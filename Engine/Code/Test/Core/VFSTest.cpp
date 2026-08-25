#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <Service/Service.h>
#include <VFS/FileSystem.h>
#include <VFS/MountTable.h>
#include <VFS/VFSSystem.h>

using namespace Spark;

namespace
{
    namespace fs = std::filesystem;

    eastl::string Normalize(const fs::path& p)
    {
        const std::string s = fs::absolute(p).lexically_normal().generic_string();
        eastl::string out(s.c_str(), s.size());
        while (out.size() > 1 && out.back() == '/')
        {
            out.pop_back();
        }
        return out;
    }
}

// ============================================================================
// Path translation. Needs nothing on disk: Mount makes the directory absolute
// without checking that it exists.
// ============================================================================

class MountTableTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_table.Mount("engine", "D:/Proj/Engine/Asset");
        m_table.Mount("project", "D:/Proj/Project/Asset");
    }

    eastl::string EngineDir()  const { return Normalize("D:/Proj/Engine/Asset"); }
    eastl::string ProjectDir() const { return Normalize("D:/Proj/Project/Asset"); }

    MountTable m_table;
};

TEST_F(MountTableTest, ToPhysicalJoinsMountDirAndRelative)
{
    EXPECT_EQ(m_table.ToPhysical("engine://Shaders/GBuffer.hlsl"),
              EngineDir() + "/Shaders/GBuffer.hlsl");
    EXPECT_EQ(m_table.ToPhysical("project://Furniture.glb"),
              ProjectDir() + "/Furniture.glb");
}

TEST_F(MountTableTest, ToPhysicalOfMountRootIsTheMountDir)
{
    EXPECT_EQ(m_table.ToPhysical("engine://"), EngineDir());
}

TEST_F(MountTableTest, ToPhysicalRejectsUnknownMount)
{
    EXPECT_TRUE(m_table.ToPhysical("nope://Foo.png").empty());
}

TEST_F(MountTableTest, ToPhysicalRejectsNonVirtualPath)
{
    // Neither a bare relative nor a bare absolute path may quietly resolve.
    EXPECT_TRUE(m_table.ToPhysical("Shaders/GBuffer.hlsl").empty());
    EXPECT_TRUE(m_table.ToPhysical("D:/Proj/Engine/Asset/Shaders/GBuffer.hlsl").empty());
    EXPECT_TRUE(m_table.ToPhysical("://Foo.png").empty());
    EXPECT_TRUE(m_table.ToPhysical("").empty());
}

TEST_F(MountTableTest, ToPhysicalNormalizesRelativeSegment)
{
    const eastl::string expected = EngineDir() + "/Shaders/GBuffer.hlsl";

    EXPECT_EQ(m_table.ToPhysical("engine://Shaders//GBuffer.hlsl"),     expected);
    EXPECT_EQ(m_table.ToPhysical("engine://./Shaders/GBuffer.hlsl"),    expected);
    EXPECT_EQ(m_table.ToPhysical("engine:///Shaders/GBuffer.hlsl"),     expected);
    EXPECT_EQ(m_table.ToPhysical("engine://Lib/../Shaders/GBuffer.hlsl"), expected);
}

// glTF external texture URIs legitimately carry ".." (`../shared/wood.png`), so it has to
// be resolved lexically rather than rejected outright.
TEST_F(MountTableTest, ToPhysicalResolvesParentSegmentLexically)
{
    EXPECT_EQ(m_table.ToPhysical("project://Models/../Textures/wood.png"),
              ProjectDir() + "/Textures/wood.png");
}

TEST_F(MountTableTest, ToPhysicalRejectsEscapeAboveMountRoot)
{
    EXPECT_TRUE(m_table.ToPhysical("project://../Engine/Asset/secret.png").empty());
    EXPECT_TRUE(m_table.ToPhysical("project://Models/../../escaped.png").empty());
}

TEST_F(MountTableTest, ToVirtualRoundTrips)
{
    const eastl::string physical = EngineDir() + "/Shaders/GBuffer.hlsl";
    const eastl::string virtualPath = m_table.ToVirtual(physical);

    EXPECT_EQ(virtualPath, "engine://Shaders/GBuffer.hlsl");
    EXPECT_EQ(m_table.ToPhysical(virtualPath), physical);
}

TEST_F(MountTableTest, ToVirtualAcceptsBackslashesAndMixedCaseDrive)
{
    EXPECT_EQ(m_table.ToVirtual("D:\\Proj\\Engine\\Asset\\Shaders\\GBuffer.hlsl"),
              "engine://Shaders/GBuffer.hlsl");
    EXPECT_EQ(m_table.ToVirtual("d:/proj/engine/asset/Shaders/GBuffer.hlsl"),
              "engine://Shaders/GBuffer.hlsl");
}

TEST_F(MountTableTest, ToVirtualRejectsPathOutsideEveryMount)
{
    EXPECT_TRUE(m_table.ToVirtual("D:/Somewhere/Else/foo.png").empty());
}

// The prefix must land on a directory boundary.
TEST_F(MountTableTest, ToVirtualDoesNotMatchPartialDirectoryName)
{
    EXPECT_TRUE(m_table.ToVirtual("D:/Proj/Engine/AssetOther/foo.png").empty());
}

TEST_F(MountTableTest, GetMountNamesAndDirsKeepRegistrationOrder)
{
    const auto names = m_table.GetMountNames();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "engine");
    EXPECT_EQ(names[1], "project");

    const auto dirs = m_table.GetPhysicalDirs();
    ASSERT_EQ(dirs.size(), 2u);
    EXPECT_EQ(dirs[0], EngineDir());
    EXPECT_EQ(dirs[1], ProjectDir());
}

TEST_F(MountTableTest, UnmountRemovesTheMount)
{
    m_table.Unmount("project");

    EXPECT_TRUE(m_table.ToPhysical("project://Furniture.glb").empty());
    EXPECT_EQ(m_table.GetMountNames().size(), 1u);
    // Other mounts are untouched.
    EXPECT_FALSE(m_table.ToPhysical("engine://Shaders/GBuffer.hlsl").empty());
}

// ============================================================================
// Mount validation
// ============================================================================

TEST(MountTableValidation, RejectsInvalidMountNames)
{
    MountTable table;
    table.Mount("",          "D:/Proj/A");
    table.Mount("a/b",       "D:/Proj/B");
    table.Mount("engine://", "D:/Proj/C");
    table.Mount("engine",    "");

    EXPECT_TRUE(table.GetMountNames().empty());
}

TEST(MountTableValidation, RejectsDuplicateMountName)
{
    MountTable table;
    table.Mount("engine", "D:/Proj/A");
    table.Mount("engine", "D:/Proj/B");

    ASSERT_EQ(table.GetMountNames().size(), 1u);
    EXPECT_EQ(table.GetPhysicalDirs()[0], Normalize("D:/Proj/A"));
}

// Nested physical directories would give one file two virtual paths, hence two AssetIds.
TEST(MountTableValidation, RejectsOverlappingPhysicalDirs)
{
    MountTable nested;
    nested.Mount("engine",  "D:/Proj/Engine/Asset");
    nested.Mount("shaders", "D:/Proj/Engine/Asset/Shaders");
    EXPECT_EQ(nested.GetMountNames().size(), 1u);

    MountTable reversed;
    reversed.Mount("shaders", "D:/Proj/Engine/Asset/Shaders");
    reversed.Mount("engine",  "D:/Proj/Engine/Asset");
    EXPECT_EQ(reversed.GetMountNames().size(), 1u);

    MountTable same;
    same.Mount("a", "D:/Proj/Engine/Asset");
    same.Mount("b", "D:/Proj/Engine/Asset/");
    EXPECT_EQ(same.GetMountNames().size(), 1u);
}

// A sibling sharing a name prefix is not an overlap.
TEST(MountTableValidation, AllowsSiblingDirectories)
{
    MountTable table;
    table.Mount("engine",  "D:/Proj/Engine/Asset");
    table.Mount("project", "D:/Proj/Engine/AssetOther");

    EXPECT_EQ(table.GetMountNames().size(), 2u);
}

// ============================================================================
// IterateDirectory. Needs real files on disk.
// ============================================================================

class IterateDirectoryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = fs::temp_directory_path() / "SparkVFSTest";

        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "Shaders" / "Lib", ec);
        fs::create_directories(m_root / "Image", ec);

        Touch(m_root / "Shaderball.glb");
        Touch(m_root / "Shaders" / "GBuffer.hlsl");
        Touch(m_root / "Shaders" / "Lib" / "BRDF.hlsli");
        Touch(m_root / "Image" / "BRDFLut.ktx2");

        m_table.Mount("engine", eastl::string(m_root.generic_string().c_str()));
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    static void Touch(const fs::path& p)
    {
        std::ofstream file(p);
        file << "x";
    }

    eastl::vector<eastl::string> Collect(const char* virtualDir)
    {
        eastl::vector<eastl::string> found;
        m_table.IterateDirectory(virtualDir, [&](eastl::string_view path)
        {
            found.push_back(eastl::string(path.data(), path.size()));
        });
        eastl::sort(found.begin(), found.end());
        return found;
    }

    fs::path   m_root;
    MountTable m_table;
};

TEST_F(IterateDirectoryTest, YieldsVirtualPathsForEveryFileRecursively)
{
    const auto found = Collect("engine://");

    ASSERT_EQ(found.size(), 4u);
    EXPECT_EQ(found[0], "engine://Image/BRDFLut.ktx2");
    EXPECT_EQ(found[1], "engine://Shaderball.glb");
    EXPECT_EQ(found[2], "engine://Shaders/GBuffer.hlsl");
    EXPECT_EQ(found[3], "engine://Shaders/Lib/BRDF.hlsli");
}

// AssetRegistry depends on nothing more than this round trip holding.
TEST_F(IterateDirectoryTest, EveryYieldedPathResolvesBack)
{
    for (const eastl::string& virtualPath : Collect("engine://"))
    {
        const eastl::string physical = m_table.ToPhysical(virtualPath);
        ASSERT_FALSE(physical.empty()) << virtualPath.c_str();
        EXPECT_TRUE(fs::exists(fs::path(physical.c_str()))) << physical.c_str();
    }
}

TEST_F(IterateDirectoryTest, ScopesToASubdirectory)
{
    const auto found = Collect("engine://Shaders");

    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0], "engine://Shaders/GBuffer.hlsl");
    EXPECT_EQ(found[1], "engine://Shaders/Lib/BRDF.hlsli");
}

TEST_F(IterateDirectoryTest, IgnoresUnknownMountAndMissingDirectory)
{
    EXPECT_TRUE(Collect("nope://").empty());
    EXPECT_TRUE(Collect("engine://DoesNotExist").empty());
    EXPECT_TRUE(Collect("engine/Shaders").empty());
}

// ============================================================================
// File IO. The asset cache is the reason these exist, so the cases are the ones
// it depends on: whole-file round trips, overwrite, and an unstampable path.
// ============================================================================

class FileIOTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = fs::temp_directory_path() / "SparkVFSIOTest";

        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root, ec);

        m_table.Mount("cache", eastl::string(m_root.generic_string().c_str()));
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    static eastl::vector<uint8_t> Bytes(std::initializer_list<uint8_t> values)
    {
        return eastl::vector<uint8_t>(values.begin(), values.end());
    }

    bool Write(const char* virtualPath, const eastl::vector<uint8_t>& data)
    {
        return m_table.WriteFile(virtualPath, data.data(), data.size());
    }

    fs::path   m_root;
    MountTable m_table;
};

TEST_F(FileIOTest, WriteThenReadRoundTrips)
{
    const eastl::vector<uint8_t> written = Bytes({0xAB, 0x00, 0xFF, 0x10});
    ASSERT_TRUE(Write("cache://3f/3fa9c2b81d4e6075.ktx2", written));

    eastl::vector<uint8_t> read;
    ASSERT_TRUE(m_table.ReadFile("cache://3f/3fa9c2b81d4e6075.ktx2", read));
    EXPECT_EQ(read, written);
}

// The shard directory does not exist until the first entry lands in it.
TEST_F(FileIOTest, WriteCreatesParentDirectories)
{
    ASSERT_FALSE(fs::exists(m_root / "3f"));
    ASSERT_TRUE(Write("cache://3f/entry.ktx2", Bytes({1})));
    EXPECT_TRUE(fs::is_directory(m_root / "3f"));
}

// No temporary may survive a completed write: the cache lists its directory to clear it.
TEST_F(FileIOTest, WriteLeavesNoTemporary)
{
    ASSERT_TRUE(Write("cache://entry.ktx2", Bytes({1, 2, 3})));

    for (const auto& entry : fs::directory_iterator(m_root))
    {
        EXPECT_EQ(entry.path().extension().generic_string(), std::string(".ktx2"))
            << entry.path().generic_string();
    }
}

TEST_F(FileIOTest, WriteReplacesExistingContent)
{
    ASSERT_TRUE(Write("cache://entry.ktx2", Bytes({1, 2, 3, 4, 5})));
    ASSERT_TRUE(Write("cache://entry.ktx2", Bytes({9})));

    eastl::vector<uint8_t> read;
    ASSERT_TRUE(m_table.ReadFile("cache://entry.ktx2", read));
    EXPECT_EQ(read, Bytes({9}));
}

TEST_F(FileIOTest, EmptyFileRoundTrips)
{
    ASSERT_TRUE(m_table.WriteFile("cache://empty.ktx2", nullptr, 0));
    EXPECT_TRUE(m_table.Exists("cache://empty.ktx2"));

    eastl::vector<uint8_t> read = Bytes({7});
    EXPECT_TRUE(m_table.ReadFile("cache://empty.ktx2", read));
    EXPECT_TRUE(read.empty());
}

TEST_F(FileIOTest, ReadRejectsMissingFileAndUnknownMount)
{
    eastl::vector<uint8_t> read;
    EXPECT_FALSE(m_table.ReadFile("cache://nothing.ktx2", read));
    EXPECT_FALSE(m_table.ReadFile("nope://nothing.ktx2", read));
    EXPECT_FALSE(m_table.WriteFile("nope://nothing.ktx2", read.data(), read.size()));
}

// A hit is decided by Exists alone, so a directory answering true would send a directory
// path into ReadFile.
TEST_F(FileIOTest, ExistsIsFalseForDirectoriesAndMissingPaths)
{
    ASSERT_TRUE(Write("cache://3f/entry.ktx2", Bytes({1})));

    EXPECT_TRUE(m_table.Exists("cache://3f/entry.ktx2"));
    EXPECT_FALSE(m_table.Exists("cache://3f"));
    EXPECT_FALSE(m_table.Exists("cache://"));
    EXPECT_FALSE(m_table.Exists("cache://missing.ktx2"));
}

TEST_F(FileIOTest, StampReportsSizeAndIsInvalidWithoutAFile)
{
    ASSERT_TRUE(Write("cache://entry.ktx2", Bytes({1, 2, 3, 4})));

    const FileStamp stamp = m_table.GetFileStamp("cache://entry.ktx2");
    EXPECT_TRUE(stamp.IsValid());
    EXPECT_EQ(stamp.m_size, 4u);

    EXPECT_FALSE(m_table.GetFileStamp("cache://missing.ktx2").IsValid());
    EXPECT_FALSE(m_table.GetFileStamp("cache://").IsValid());
    EXPECT_FALSE(m_table.GetFileStamp("nope://entry.ktx2").IsValid());
}

// The key folds the stamp in, so a rewrite that changes the size has to be visible;
// otherwise a rebuilt source would keep resolving to its stale entry.
TEST_F(FileIOTest, StampChangesWithContent)
{
    ASSERT_TRUE(Write("cache://entry.ktx2", Bytes({1, 2, 3, 4})));
    const FileStamp before = m_table.GetFileStamp("cache://entry.ktx2");

    ASSERT_TRUE(Write("cache://entry.ktx2", Bytes({1, 2})));
    const FileStamp after = m_table.GetFileStamp("cache://entry.ktx2");

    EXPECT_NE(before.m_size, after.m_size);
}

// ============================================================================
// VFSSystem registration and forwarding
// ============================================================================

TEST(VFSSystemTest, RegistersItselfOnConstructionAndForwards)
{
    ASSERT_EQ(Service<FileSystem>::Get(), nullptr);

    {
        SystemUniquePtr<VFSSystem> vfs = CreateSystem<VFSSystem>();
        vfs->Init();

        // The Handler constructor registers, so this holds before Init().
        FileSystem* resolved = Service<FileSystem>::Get();
        ASSERT_EQ(resolved, static_cast<FileSystem*>(vfs.get()));

        resolved->Mount("engine", "D:/Proj/Engine/Asset");
        EXPECT_EQ(resolved->ToPhysical("engine://Shaders/GBuffer.hlsl"),
                  Normalize("D:/Proj/Engine/Asset") + "/Shaders/GBuffer.hlsl");
        EXPECT_EQ(vfs->GetMountNames().size(), 1u);
    }

    EXPECT_EQ(Service<FileSystem>::Get(), nullptr);
}
