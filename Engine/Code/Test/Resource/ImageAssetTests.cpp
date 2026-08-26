#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <Resource/AssetManager.h>
#include <VFS/MountTable.h>
#include <VFS/VFSSystem.h>
#include <Resource/Cache/AssetCache.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Image/ImageAssetLoader.h>
#include <Resource/Image/ImageAssetCompiler.h>
#include <Resource/Image/EnvironmentBaker.h>

using namespace Spark;
using namespace Spark::Resource;

// Both mounts the Resource tests need. Kept non-overlapping: IMAGE_ASSET_DIR used to nest
// inside ENGINE_ASSET_DIR, and MODEL_ASSET_DIR inside TEST_RESOURCE_DIR, which the mount
// table now rejects.
static void SetUpMounts(Spark::FileSystem& table)
{
    table.Mount("engine", ENGINE_ASSET_DIR);
    table.Mount("test", TEST_RESOURCE_DIR);
}

//! Its own cache directory per test. A shared one would make these order-dependent and let
//! a stale entry from an earlier run hide a real compiler bug.
class ImageAssetTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_cacheDir = std::filesystem::temp_directory_path()
                   / "SparkImageCache"
                   / ::testing::UnitTest::GetInstance()->current_test_info()->name();

        std::error_code ec;
        std::filesystem::remove_all(m_cacheDir, ec);
        std::filesystem::create_directories(m_cacheDir, ec);

        m_vfs = CreateSystem<VFSSystem>();
        m_vfs->Init();
        SetUpMounts(*m_vfs);
        m_vfs->Mount(kCacheMountName, eastl::string(m_cacheDir.generic_string().c_str()));

        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
    }

    void TearDown() override
    {
        m_assetManager.reset();
        m_vfs.reset();

        std::error_code ec;
        std::filesystem::remove_all(m_cacheDir, ec);
    }

    //! Tear the manager down and stand a new one up over the same mounts and cache: the
    //! only way to build an asset twice, since within one manager the database returns the
    //! first instance and never rebuilds.
    //!
    //! Sequential, never two at once. Asset::Shutdown routes through
    //! Service<AssetManager>::Get(), which only ever names one of them, so a second live
    //! manager would tear its assets out of the other one's database.
    void Restart()
    {
        m_assetManager.reset();
        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
    }

    //! The payload files alone. Each entry also drops a `.unit` manifest beside them, which
    //! is what makes the unit complete rather than part of what a builder produced.
    eastl::vector<std::filesystem::path> PayloadEntries() const
    {
        eastl::vector<std::filesystem::path> found;
        for (const std::filesystem::path& path : CacheEntries())
        {
            if (path.extension().generic_string() != ".unit")
            {
                found.push_back(path);
            }
        }
        return found;
    }

    //! Every file under the cache mount, shard directories walked.
    eastl::vector<std::filesystem::path> CacheEntries() const
    {
        eastl::vector<std::filesystem::path> found;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_cacheDir, ec),
                  end = std::filesystem::recursive_directory_iterator();
             it != end; it.increment(ec))
        {
            if (!it->is_directory(ec))
            {
                found.push_back(it->path());
            }
        }
        return found;
    }

    std::filesystem::path               m_cacheDir;
    SystemUniquePtr<VFSSystem>          m_vfs;
    SystemUniquePtr<SparkAssetManager>  m_assetManager;
};


TEST(ImageAssetRawDataTest, BytesPerPixelR8)
{
    eastl::vector<uint8_t> pixels(16);
    ImageAssetRawData data(4, 4, ImageFormat::R8, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 1u);
    EXPECT_FALSE(data.IsHDR());
}

TEST(ImageAssetRawDataTest, BytesPerPixelRG8)
{
    eastl::vector<uint8_t> pixels(32);
    ImageAssetRawData data(4, 4, ImageFormat::RG8, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 2u);
    EXPECT_FALSE(data.IsHDR());
}

TEST(ImageAssetRawDataTest, BytesPerPixelRGBA8)
{
    eastl::vector<uint8_t> pixels(64);
    ImageAssetRawData data(4, 4, ImageFormat::RGBA8, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 4u);
    EXPECT_FALSE(data.IsHDR());
}

TEST(ImageAssetRawDataTest, BytesPerPixelRGBAF32)
{
    eastl::vector<uint8_t> pixels(256);
    ImageAssetRawData data(4, 4, ImageFormat::RGBAF32, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 16u);
    EXPECT_TRUE(data.IsHDR());
}


TEST(ImageAssetLoaderTest, LoadMissingFileReturnsNull)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ImageAssetLoader loader;

    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/non_existent.png");
    EXPECT_EQ(loader.LoadSource(id, fileSystem), nullptr);
}

TEST(ImageAssetLoaderTest, LoadJpegAsRGBA8)
{
    // JPEG 是 3 通道 RGB，Loader 应将其升级为 RGBA8
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ImageAssetLoader loader;

    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_diff_2k.jpg");
    auto data = loader.LoadSource(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* imgData = static_cast<ImageAssetRawData*>(data.get());
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::RGBA8);
    EXPECT_EQ(imgData->GetWidth(), 2048);
    EXPECT_EQ(imgData->GetHeight(), 2048);

    const size_t expectedBytes = static_cast<size_t>(imgData->GetWidth())
                               * imgData->GetHeight() * imgData->GetBytesPerPixel();
    EXPECT_EQ(imgData->GetPixels().size(), expectedBytes);
    EXPECT_FALSE(imgData->GetResolvedPath().empty());
}

TEST(ImageAssetLoaderTest, LoadAoJpegAsRGBA8)
{
    // 单通道灰度 JPEG 也要解成 RGBA8：灰度是编码器的体积优化，不是通道数声明。
    // 留成 R8 的话采样会得到 (r, 0, 0, 1)，纯白贴图会变成纯红。
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ImageAssetLoader loader;

    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_ao_2k.jpg");
    auto data = loader.LoadSource(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* imgData = static_cast<ImageAssetRawData*>(data.get());
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::RGBA8);
    EXPECT_EQ(imgData->GetWidth(), 2048);
    EXPECT_EQ(imgData->GetHeight(), 2048);

    // JPEG 的灰度展开在 stb 里是独立于 PNG 的一条代码路径，单独验
    const auto& pixels = imgData->GetPixels();
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        ASSERT_EQ(pixels[i + 1], pixels[i]);
        ASSERT_EQ(pixels[i + 2], pixels[i]);
        ASSERT_EQ(pixels[i + 3], 255);
    }
}

TEST(ImageAssetLoaderTest, LoadDisplacementPngExpandsGrayscale)
{
    // 灰度 PNG 展开的语义是 R=G=B=gray, A=255
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ImageAssetLoader loader;

    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_disp_2k.png");
    auto data = loader.LoadSource(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* imgData = static_cast<ImageAssetRawData*>(data.get());
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::RGBA8);
    EXPECT_GT(imgData->GetWidth(), 0);
    EXPECT_GT(imgData->GetHeight(), 0);

    const size_t expectedBytes = static_cast<size_t>(imgData->GetWidth())
                               * imgData->GetHeight() * imgData->GetBytesPerPixel();
    const auto& pixels = imgData->GetPixels();
    ASSERT_EQ(pixels.size(), expectedBytes);

    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        ASSERT_EQ(pixels[i + 1], pixels[i]);
        ASSERT_EQ(pixels[i + 2], pixels[i]);
        ASSERT_EQ(pixels[i + 3], 255);
    }
}

TEST(ImageAssetLoaderTest, ExrNotSupportedReturnsNull)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ImageAssetLoader loader;

    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_rough_2k.exr");
    EXPECT_EQ(loader.LoadSource(id, fileSystem), nullptr);
}


TEST_F(ImageAssetTestFixture, LoadImageAssetSync)
{
    // Loader + Compiler 由 AssetManager::Init() 默认注册，无需手动 register
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_diff_2k.jpg");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsReady());
    EXPECT_EQ(asset->GetAssetType(), AssetType::Image);

    // 走完整 pipeline 后拿到的是 compiled ImageAssetData（默认 hint = BC3_RGBA + sRGB）
    auto* imgData = asset->GetData<ImageAssetData>();
    ASSERT_NE(imgData, nullptr);
    EXPECT_EQ(imgData->GetFormat(), RHI::Format::BC3_UNORM_SRGB);
    EXPECT_GT(imgData->GetWidth(), 0u);
    EXPECT_GT(imgData->GetHeight(), 0u);
    EXPECT_GT(imgData->GetMipLevels(), 1u);   // 默认会生成完整 mip chain
}

TEST_F(ImageAssetTestFixture, LoadSameImageReturnsCached)
{
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_diff_2k.jpg");
    Ptr<Asset> asset1 = m_assetManager->LoadAsset(id);
    Ptr<Asset> asset2 = m_assetManager->LoadAsset(id);

    ASSERT_NE(asset1, nullptr);
    EXPECT_EQ(asset1.get(), asset2.get());
}

TEST_F(ImageAssetTestFixture, LoadNonExistentImageReturnsError)
{
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/non_existent.png");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsError());
}

TEST_F(ImageAssetTestFixture, LoadJpegProducesBC3)
{
    // 验证整条 pipeline：JPG → decode → resize → BC3 编码 → KTX2 容器
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_diff_2k.jpg");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id);

    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(asset->IsReady());

    auto* imgData = asset->GetData<ImageAssetData>();
    ASSERT_NE(imgData, nullptr);

    // 维度
    EXPECT_EQ(imgData->GetWidth(),  2048u);
    EXPECT_EQ(imgData->GetHeight(), 2048u);
    EXPECT_EQ(imgData->GetMipLevels(),   12u);  // floor(log2(2048)) + 1
    EXPECT_EQ(imgData->GetArrayLayers(),  1u);

    // 格式（默认 hint = BC3 + sRGB）
    EXPECT_EQ(imgData->GetFormat(), RHI::Format::BC3_UNORM_SRGB);

    // Base mip：2048x2048 → 512x512 个 4x4 block，每 block 16 字节 = 4MB
    const uint64_t expectedBaseBytes =
        static_cast<uint64_t>(2048 / 4) * (2048 / 4) * 16;
    EXPECT_EQ(imgData->GetMipRange(0).size, expectedBaseBytes);

    // 总字节 ≈ base × 4/3（完整 BCn mip chain 的几何级数和）
    const uint64_t totalBytes = imgData->GetTextureBytes().size();
    EXPECT_GT(totalBytes, expectedBaseBytes);                    // 大于 base
    EXPECT_LT(totalBytes, expectedBaseBytes * 2);                // 远小于 base × 2

    // mip 1 应该是 base 的约 1/4
    const uint64_t mip1Bytes = imgData->GetMipRange(1).size;
    EXPECT_EQ(mip1Bytes, expectedBaseBytes / 4);

    // 最小 mip 也至少占一个 block
    const uint64_t lastMipBytes = imgData->GetMipRange(imgData->GetMipLevels() - 1).size;
    EXPECT_EQ(lastMipBytes, 16u);  // 1x1 mip → 1 block × 16 字节
}

// A .ktx2 is a source format whose Compile is a parse: Load hands back its bytes and the
// compiler parses them rather than running the pixel path. The mip count and the format
// are what prove it -- the default image descriptor asks for a full BCn/sRGB chain, so the
// pixel path would produce 8 mips of BC3_UNORM_SRGB instead.
TEST_F(ImageAssetTestFixture, Ktx2LoadsAsCompiledDataWithoutRecompiling)
{
    AssetId id = m_assetManager->MakeAssetId("engine://Image/BRDFLut.ktx2");
    ASSERT_TRUE(id.IsValid());

    Ptr<Asset> asset = m_assetManager->LoadAsset(id);
    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(asset->IsReady());

    auto* imgData = asset->GetData<ImageAssetData>();
    ASSERT_NE(imgData, nullptr);

    EXPECT_EQ(imgData->GetWidth(),  128u);
    EXPECT_EQ(imgData->GetHeight(), 128u);
    EXPECT_EQ(imgData->GetMipLevels(),   1u);
    EXPECT_EQ(imgData->GetArrayLayers(), 1u);
    EXPECT_EQ(imgData->GetFormat(), RHI::Format::R16G16_FLOAT);

    const uint64_t expectedBytes = 128ull * 128ull * 4ull;   // RG16F
    EXPECT_EQ(imgData->GetTextureBytes().size(), expectedBytes);
    EXPECT_EQ(imgData->GetMipRange(0).offset, 0u);
    EXPECT_EQ(imgData->GetMipRange(0).size, expectedBytes);
}


// ============================================================================
// Cubemap payloads
//
// BakedCubemap is a plain struct, so everything downstream of a real bake can be driven
// from a hand-built payload -- the only cube coverage that needs no GPU.
// ============================================================================

namespace
{
    constexpr uint32_t kCubeBytesPerPixel = 8;   // R16G16B16A16_FLOAT

    //! Every byte derived from its (face, mip, index), so a round trip that swaps or drops
    //! a subresource cannot still compare equal.
    BakedCubemap MakeTestCube(uint32_t faceSize, uint32_t mipLevels)
    {
        BakedCubemap cube;
        cube.faceSize  = faceSize;
        cube.mipLevels = mipLevels;
        cube.format    = RHI::Format::R16G16B16A16_FLOAT;

        for (uint32_t face = 0; face < 6; ++face)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                const uint32_t extent = std::max(1u, faceSize >> mip);
                const size_t   bytes  = static_cast<size_t>(extent) * extent * kCubeBytesPerPixel;
                for (size_t i = 0; i < bytes; ++i)
                {
                    cube.faceBytes.push_back(static_cast<uint8_t>((face * 37 + mip * 11 + i) & 0xFF));
                }
            }
        }
        return cube;
    }
}

//! Pinned here because the upload path recomputes extents instead of reading this table,
//! so a wrong table shows up as garbled pixels rather than a failure.
TEST(ImageCubemapTest, AssembledCubeIsFaceMajorAndMipInner)
{
    constexpr uint32_t kFaceSize = 8;
    constexpr uint32_t kMips     = 3;   // 8, 4, 2

    ImageAssetCompiler compiler;
    UniquePtr<AssetData> assembled = compiler.AssembleCubemapData(MakeTestCube(kFaceSize, kMips));
    ASSERT_NE(assembled, nullptr);

    const auto& cube = static_cast<const ImageAssetData&>(*assembled);
    EXPECT_TRUE(cube.IsCubemap());
    EXPECT_EQ(cube.GetArrayLayers(), 6u);
    EXPECT_EQ(cube.GetMipLevels(),   kMips);
    EXPECT_EQ(cube.GetWidth(),       kFaceSize);
    EXPECT_EQ(cube.GetHeight(),      kFaceSize);

    const uint64_t faceChainBytes = (8ull * 8 + 4ull * 4 + 2ull * 2) * kCubeBytesPerPixel;
    EXPECT_EQ(cube.GetTextureBytes().size(), faceChainBytes * 6);

    uint64_t expectedOffset = 0;
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < kMips; ++mip)
        {
            const uint32_t extent = kFaceSize >> mip;
            const ImageMipRange& range = cube.GetSubresourceRange(face, mip);
            EXPECT_EQ(range.offset, expectedOffset) << "face " << face << " mip " << mip;
            EXPECT_EQ(range.size,
                static_cast<uint64_t>(extent) * extent * kCubeBytesPerPixel);
            expectedOffset += range.size;
        }
    }

    // Slice 0's chain is exactly one face, so the 2D alias still reads the +X face.
    EXPECT_EQ(cube.GetSubresourceRange(1, 0).offset, faceChainBytes);
    EXPECT_EQ(cube.GetMipRange(0).offset, 0u);
}

//! KTX2 groups a level's faces together, the engine packs slice-major. Both sides of that
//! translation are ours, so only a round trip proves they agree.
TEST(ImageCubemapTest, ACubeSurvivesAKtx2RoundTrip)
{
    constexpr uint32_t kFaceSize = 8;
    constexpr uint32_t kMips     = 3;
    constexpr const char* kIdentity = "cube-round-trip-identity";

    ImageAssetCompiler compiler;
    UniquePtr<AssetData> assembled = compiler.AssembleCubemapData(MakeTestCube(kFaceSize, kMips));
    ASSERT_NE(assembled, nullptr);
    const auto& original = static_cast<const ImageAssetData&>(*assembled);

    const eastl::vector<uint8_t> blob = compiler.SerializeToKtx2(original, kIdentity);
    ASSERT_FALSE(blob.empty());

    ImageAssetLoader loader;
    UniquePtr<AssetData> reloaded =
        loader.LoadKtx2(blob.data(), blob.size(), "cube round trip", kIdentity);
    ASSERT_NE(reloaded, nullptr);
    const auto& restored = static_cast<const ImageAssetData&>(*reloaded);

    EXPECT_TRUE(restored.IsCubemap());
    EXPECT_EQ(restored.GetArrayLayers(), original.GetArrayLayers());
    EXPECT_EQ(restored.GetMipLevels(),   original.GetMipLevels());
    EXPECT_EQ(restored.GetWidth(),       original.GetWidth());
    EXPECT_EQ(restored.GetHeight(),      original.GetHeight());
    EXPECT_EQ(restored.GetFormat(),      original.GetFormat());
    EXPECT_EQ(restored.GetTextureBytes(), original.GetTextureBytes());

    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < kMips; ++mip)
        {
            EXPECT_EQ(restored.GetSubresourceRange(face, mip).offset,
                      original.GetSubresourceRange(face, mip).offset)
                << "face " << face << " mip " << mip;
            EXPECT_EQ(restored.GetSubresourceRange(face, mip).size,
                      original.GetSubresourceRange(face, mip).size);
        }
    }
}

//! The cube path must not have loosened the identity check the cache relies on.
TEST(ImageCubemapTest, ACubeWithAForeignIdentityIsRejected)
{
    ImageAssetCompiler compiler;
    UniquePtr<AssetData> assembled = compiler.AssembleCubemapData(MakeTestCube(4, 1));
    ASSERT_NE(assembled, nullptr);

    const eastl::vector<uint8_t> blob =
        compiler.SerializeToKtx2(static_cast<const ImageAssetData&>(*assembled), "mine");
    ASSERT_FALSE(blob.empty());

    ImageAssetLoader loader;
    EXPECT_EQ(loader.LoadKtx2(blob.data(), blob.size(), "cube identity", "someone else's"),
              nullptr);
}

// ============================================================================
// IBL child ids
//
// Derived, never stored: the build path and a future cache hit must name the same two
// children, and nothing in the KTX2 payload says who they are.
// ============================================================================

namespace
{
    Ptr<ImageAsset> MakeImage(const char* path, ImageUsage usage)
    {
        return Ptr<ImageAsset>(new ImageAsset(
            AssetId::Of(path, {}, AssetType::Image, ImageAsset::DescriptorForUsage(usage))));
    }
}

TEST(ImageIblIdTest, AnEnvironmentCubemapDerivesItsTwoChildIds)
{
    constexpr const char* kPath = "engine://Image/sky.hdr";
    Ptr<ImageAsset> sky = MakeImage(kPath, ImageUsage::EnvironmentCubemap);

    const AssetId irradiance  = sky->IrradianceId();
    const AssetId prefiltered = sky->PrefilteredId();

    ASSERT_TRUE(irradiance.IsValid());
    ASSERT_TRUE(prefiltered.IsValid());
    EXPECT_TRUE(irradiance.IsSubAsset());
    EXPECT_TRUE(prefiltered.IsSubAsset());
    EXPECT_NE(irradiance, prefiltered);

    // Same path as the parent -- a sub-asset's bytes live in the parent's file.
    EXPECT_EQ(irradiance.GetPath(), sky->GetAssetId().GetPath());

    // Exactly what the builder publishes under, so the two cannot drift apart.
    EXPECT_EQ(irradiance, ImageAsset::MakeSubId(sky->GetAssetId(),
        ImageAsset::kIrradianceSubLabel, ImageUsage::IrradianceCubemap));
    EXPECT_EQ(prefiltered, ImageAsset::MakeSubId(sky->GetAssetId(),
        ImageAsset::kPrefilteredSubLabel, ImageUsage::PrefilteredCubemap));

    // A pure function of the parent id: a second instance of the same asset agrees.
    EXPECT_EQ(irradiance, MakeImage(kPath, ImageUsage::EnvironmentCubemap)->IrradianceId());
}

//! Not an error -- this is the "no IBL here" answer the lighting path gates on.
TEST(ImageIblIdTest, APlainTextureHasNoIblChildren)
{
    Ptr<ImageAsset> texture = MakeImage("engine://Image/albedo.png", ImageUsage::Texture2D);

    EXPECT_FALSE(texture->IrradianceId().IsValid());
    EXPECT_FALSE(texture->PrefilteredId().IsValid());
    EXPECT_EQ(texture->GetIrradianceAsset(), nullptr);
    EXPECT_EQ(texture->GetPrefilteredAsset(), nullptr);
}

// ============================================================================
// Cook cache
// ============================================================================

namespace
{
    constexpr const char* kCachedImage = "engine://Image/Test/rusty_metal_04_diff_2k.jpg";

    eastl::vector<uint8_t> ReadAll(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::string   bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        return eastl::vector<uint8_t>(bytes.begin(), bytes.end());
    }
}

TEST_F(ImageAssetTestFixture, CompilingAnImageWritesOneCacheEntry)
{
    ASSERT_TRUE(CacheEntries().empty());

    Ptr<Asset> asset = m_assetManager->LoadAsset(m_assetManager->MakeAssetId(kCachedImage));
    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(asset->IsReady());

    const auto payloads = PayloadEntries();
    ASSERT_EQ(payloads.size(), 1u);
    EXPECT_EQ(payloads[0].extension().generic_string(), std::string(".ktx2"));
    EXPECT_GT(std::filesystem::file_size(payloads[0]), 0u);

    // Plus the manifest that marks the unit complete.
    EXPECT_EQ(CacheEntries().size(), 2u);
}

//! The point of the whole phase: the second build reads the entry instead of decoding the
//! JPEG and running BCn again, and lands on the same pixels.
TEST_F(ImageAssetTestFixture, ASecondRunRestoresTheSamePayloadFromCache)
{
    const AssetId id = m_assetManager->MakeAssetId(kCachedImage);

    uint32_t               width = 0, height = 0, mips = 0, layers = 0;
    RHI::Format            format = RHI::Format::Unknown;
    eastl::vector<uint8_t> pixels;
    {
        Ptr<Asset> first = m_assetManager->LoadAsset(id);
        ASSERT_TRUE(first && first->IsReady());
        const auto* cooked = first->GetData<ImageAssetData>();
        ASSERT_NE(cooked, nullptr);

        width  = cooked->GetWidth();
        height = cooked->GetHeight();
        mips   = cooked->GetMipLevels();
        layers = cooked->GetArrayLayers();
        format = cooked->GetFormat();
        pixels = cooked->GetTextureBytes();
    }
    ASSERT_EQ(PayloadEntries().size(), 1u);

    Restart();

    Ptr<Asset> restored = m_assetManager->LoadAsset(id);
    ASSERT_TRUE(restored && restored->IsReady());

    const auto* fromCache = restored->GetData<ImageAssetData>();
    ASSERT_NE(fromCache, nullptr);
    EXPECT_EQ(fromCache->GetWidth(),       width);
    EXPECT_EQ(fromCache->GetHeight(),      height);
    EXPECT_EQ(fromCache->GetMipLevels(),   mips);
    EXPECT_EQ(fromCache->GetArrayLayers(), layers);
    EXPECT_EQ(fromCache->GetFormat(),      format);
    EXPECT_EQ(fromCache->GetTextureBytes(), pixels);

    // Still one entry: a hit must not write anything back.
    EXPECT_EQ(PayloadEntries().size(), 1u);
}

TEST_F(ImageAssetTestFixture, ATruncatedEntryIsRejectedAndRebuilt)
{
    const AssetId id = m_assetManager->MakeAssetId(kCachedImage);
    ASSERT_TRUE(m_assetManager->LoadAsset(id)->IsReady());

    const auto payloads = PayloadEntries();
    ASSERT_EQ(payloads.size(), 1u);
    const eastl::vector<uint8_t> intact = ReadAll(payloads[0]);
    ASSERT_GT(intact.size(), 1024u);

    {
        std::ofstream truncate(payloads[0], std::ios::binary | std::ios::trunc);
        truncate.write(reinterpret_cast<const char*>(intact.data()), 512);
    }

    Restart();
    Ptr<Asset> rebuilt = m_assetManager->LoadAsset(id);
    ASSERT_TRUE(rebuilt && rebuilt->IsReady());

    // Rebuilt from source and written back over the same path, so the entry is whole again.
    ASSERT_EQ(PayloadEntries().size(), 1u);
    EXPECT_EQ(ReadAll(payloads[0]), intact);
}

//! A key collision would land one asset on another's entry. The identity stored inside the
//! payload is what catches it, so corrupt that alone and the entry must be refused.
TEST_F(ImageAssetTestFixture, AnEntryWithAForeignIdentityIsRejected)
{
    const AssetId id = m_assetManager->MakeAssetId(kCachedImage);
    ASSERT_TRUE(m_assetManager->LoadAsset(id)->IsReady());

    const auto payloads = PayloadEntries();
    ASSERT_EQ(payloads.size(), 1u);

    const eastl::vector<uint8_t> intact = ReadAll(payloads[0]);
    eastl::vector<uint8_t>       forged = intact;

    const eastl::string_view needle("engine://Image/Test/");
    const auto* found = std::search(forged.begin(), forged.end(), needle.begin(), needle.end());
    ASSERT_NE(found, forged.end()) << "identity should be stored verbatim in the KV data";

    // One byte of the path is enough: the comparison is exact. Everything else about the
    // container stays valid, so only the identity check can reject this.
    forged[static_cast<size_t>(found - forged.begin())] = 'X';
    {
        std::ofstream rewrite(payloads[0], std::ios::binary | std::ios::trunc);
        rewrite.write(reinterpret_cast<const char*>(forged.data()), forged.size());
    }

    Restart();
    Ptr<Asset> rebuilt = m_assetManager->LoadAsset(id);
    ASSERT_TRUE(rebuilt && rebuilt->IsReady());

    // Rebuilt from source, so the forged entry is gone.
    EXPECT_EQ(ReadAll(payloads[0]), intact);
}

//! Its cooked form IS its source file, so caching it would copy that file and then never
//! read the copy -- the same Load path wins again next time.
TEST_F(ImageAssetTestFixture, AnAuthoredKtx2IsNeverWrittenBack)
{
    Ptr<Asset> asset =
        m_assetManager->LoadAsset(m_assetManager->MakeAssetId("engine://Image/BRDFLut.ktx2"));
    ASSERT_TRUE(asset && asset->IsReady());

    EXPECT_TRUE(CacheEntries().empty());
}
