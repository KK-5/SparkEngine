#include <gtest/gtest.h>

#include <filesystem>

#include <VFS/MountTable.h>

#include <Resource/Cache/AssetCache.h>
#include <Resource/Cache/CacheFormat.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Shader/ShaderAsset.h>

using namespace Spark;
using namespace Spark::Resource;

namespace
{
    namespace fs = std::filesystem;

    AssetId ImageId(const char* virtualPath, ImageUsage usage = ImageUsage::Texture2D)
    {
        return AssetId::Of(virtualPath, {}, AssetType::Image,
                           ImageAsset::DescriptorForUsage(usage));
    }
}

//! A private cache directory plus a real source file: EntryFor reads the source's stamp.
class AssetCacheTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = fs::temp_directory_path() / "SparkAssetCacheTest";

        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "Cache", ec);
        fs::create_directories(m_root / "Source", ec);

        WriteSource("Texture.png", "some source bytes");

        m_table.Mount(kCacheMountName, eastl::string((m_root / "Cache").generic_string().c_str()));
        m_table.Mount("test", eastl::string((m_root / "Source").generic_string().c_str()));
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    void WriteSource(const char* name, const std::string& contents)
    {
        const std::string bytes = contents;
        MountTable writer;
        writer.Mount("scratch", eastl::string((m_root / "Source").generic_string().c_str()));

        eastl::string path = "scratch://";
        path += name;
        writer.WriteFile(path, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    }

    fs::path   m_root;
    MountTable m_table;
};

TEST_F(AssetCacheTestFixture, EntryIsStableAcrossCalls)
{
    const AssetCache cache(m_table);

    const CacheEntry first  = cache.EntryFor(ImageId("test://Texture.png"));
    const CacheEntry second = cache.EntryFor(ImageId("test://Texture.png"));

    ASSERT_TRUE(first.IsCacheable());
    EXPECT_EQ(first.path, second.path);
    EXPECT_EQ(first.identity, second.identity);
}

TEST_F(AssetCacheTestFixture, EntryPathIsShardedUnderTheCacheMount)
{
    const AssetCache cache(m_table);

    const CacheEntry entry = cache.EntryFor(ImageId("test://Texture.png"));
    ASSERT_TRUE(entry.IsCacheable());

    // cache://<2 hex>/<16 hex>.ktx2
    const eastl::string prefix = eastl::string(kCacheMountName) + "://";
    EXPECT_EQ(entry.path.substr(0, prefix.size()), prefix);
    EXPECT_EQ(entry.path.size(), prefix.size() + 2 + 1 + 16 + 5);
    EXPECT_EQ(entry.path[prefix.size() + 2], '/');
    EXPECT_EQ(entry.path.substr(entry.path.size() - 5), ".ktx2");

    // The shard is the key's leading byte, so it repeats the file name's first two.
    EXPECT_EQ(entry.path.substr(prefix.size(), 2), entry.path.substr(prefix.size() + 3, 2));
}

//! Why the key folds in the serialized descriptor rather than its hash: same file, differing
//! only in colour space, and they must not share an entry.
TEST_F(AssetCacheTestFixture, UsageVariantsOfOneFileGetDifferentEntries)
{
    const AssetCache cache(m_table);

    const CacheEntry colour = cache.EntryFor(ImageId("test://Texture.png", ImageUsage::Texture2D));
    const CacheEntry normal = cache.EntryFor(ImageId("test://Texture.png", ImageUsage::NormalMap));

    ASSERT_TRUE(colour.IsCacheable());
    ASSERT_TRUE(normal.IsCacheable());
    EXPECT_NE(colour.path, normal.path);
    EXPECT_NE(colour.identity, normal.identity);
}

//! Why nothing detects staleness: a rewritten source addresses a different entry.
TEST_F(AssetCacheTestFixture, RewritingTheSourceMovesTheEntry)
{
    const AssetCache cache(m_table);

    const CacheEntry before = cache.EntryFor(ImageId("test://Texture.png"));
    ASSERT_TRUE(before.IsCacheable());

    WriteSource("Texture.png", "a longer set of source bytes");

    const CacheEntry after = cache.EntryFor(ImageId("test://Texture.png"));
    ASSERT_TRUE(after.IsCacheable());
    EXPECT_NE(before.path, after.path);

    // The source's stamp feeds the key, not the identity, so this is what does NOT move.
    EXPECT_EQ(before.identity, after.identity);
}

TEST_F(AssetCacheTestFixture, SubAssetsAreNotCacheable)
{
    const AssetCache cache(m_table);

    const AssetId parent = ImageId("test://Texture.png");
    const AssetId child  = ImageAsset::MakeSubId(parent, "ibl/irradiance",
                                                 ImageUsage::IrradianceCubemap);

    EXPECT_FALSE(cache.EntryFor(child).IsCacheable());
}

//! The rule is on the format's extension, not on ".ktx2" spelled out in the cache, so a
//! second type that authors its own cooked form is covered for free.
TEST_F(AssetCacheTestFixture, ASourceAlreadyInTheCacheFormatIsNotCacheable)
{
    const AssetCache cache(m_table);

    WriteSource("Authored.ktx2", "ktx2 bytes");

    EXPECT_FALSE(cache.EntryFor(ImageId("test://Authored.ktx2")).IsCacheable());
    EXPECT_TRUE(cache.EntryFor(ImageId("test://Texture.png")).IsCacheable());
}

TEST_F(AssetCacheTestFixture, TypesWithoutACacheFormatAreNotCacheable)
{
    const AssetCache cache(m_table);

    WriteSource("Thing.hlsl", "// shader");
    WriteSource("Thing.glb", "glb");

    EXPECT_FALSE(cache.EntryFor(AssetId::Of<ShaderAsset>("test://Thing.hlsl")).IsCacheable());
    EXPECT_FALSE(cache.EntryFor(AssetId::Of<ModelAsset>("test://Thing.glb")).IsCacheable());
}

TEST_F(AssetCacheTestFixture, MissingSourceIsNotCacheable)
{
    const AssetCache cache(m_table);

    EXPECT_FALSE(cache.EntryFor(ImageId("test://NotThere.png")).IsCacheable());
}

TEST_F(AssetCacheTestFixture, ReadMissesBeforeAnythingIsWritten)
{
    const AssetCache cache(m_table);

    const CacheEntry entry = cache.EntryFor(ImageId("test://Texture.png"));
    ASSERT_TRUE(entry.IsCacheable());

    eastl::vector<uint8_t> blob;
    EXPECT_FALSE(cache.Read(entry, blob));
}

TEST_F(AssetCacheTestFixture, WriteThenReadRoundTrips)
{
    const AssetCache cache(m_table);

    const CacheEntry entry = cache.EntryFor(ImageId("test://Texture.png"));
    ASSERT_TRUE(entry.IsCacheable());

    const eastl::vector<uint8_t> written = {0x01, 0x02, 0x03};
    ASSERT_TRUE(cache.Write(entry, written));

    eastl::vector<uint8_t> read;
    ASSERT_TRUE(cache.Read(entry, read));
    EXPECT_EQ(read, written);
}

//! How the sandbox programs and the other fixtures keep behaving as they did before.
TEST_F(AssetCacheTestFixture, AnUnmountedCacheDisablesEverything)
{
    MountTable sourcesOnly;
    sourcesOnly.Mount("test", eastl::string((m_root / "Source").generic_string().c_str()));

    const AssetCache cache(sourcesOnly);
    const CacheEntry entry = cache.EntryFor(ImageId("test://Texture.png"));

    EXPECT_FALSE(entry.IsCacheable());

    eastl::vector<uint8_t> blob = {1};
    EXPECT_FALSE(cache.Write(entry, blob));
    EXPECT_FALSE(cache.Read(entry, blob));
}

TEST(CacheFormatTest, OnlyImageHasAFormatToday)
{
    EXPECT_EQ(GetCacheFormat(AssetType::Image).version, 1u);
    EXPECT_STREQ(GetCacheFormat(AssetType::Image).extension, ".ktx2");

    EXPECT_EQ(GetCacheFormat(AssetType::Shader).version, 0u);
    EXPECT_EQ(GetCacheFormat(AssetType::Model).version, 0u);
    EXPECT_EQ(GetCacheFormat(AssetType::Unknown).version, 0u);
}
