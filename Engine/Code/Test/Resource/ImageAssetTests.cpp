#include <gtest/gtest.h>

#include <Resource/AssetManager.h>
#include <VFS/MountTable.h>
#include <VFS/VFSSystem.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Image/ImageAssetLoader.h>

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

class ImageAssetTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_vfs = CreateSystem<VFSSystem>();
        m_vfs->Init();
        SetUpMounts(*m_vfs);

        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
    }

    void TearDown() override
    {
        m_assetManager.reset();
        m_vfs.reset();
    }

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

    bool isCompiled = false;
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/non_existent.png");
    EXPECT_EQ(loader.Load(id, fileSystem, isCompiled), nullptr);
}

TEST(ImageAssetLoaderTest, LoadJpegAsRGBA8)
{
    // JPEG 是 3 通道 RGB，Loader 应将其升级为 RGBA8
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ImageAssetLoader loader;

    bool isCompiled = false;
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_diff_2k.jpg");
    auto data = loader.Load(id, fileSystem, isCompiled);
    ASSERT_NE(data, nullptr);
    ASSERT_FALSE(isCompiled);   // decoded source pixels, not a compiled payload

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

    bool isCompiled = false;
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_ao_2k.jpg");
    auto data = loader.Load(id, fileSystem, isCompiled);
    ASSERT_NE(data, nullptr);
    ASSERT_FALSE(isCompiled);

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

    bool isCompiled = false;
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_disp_2k.png");
    auto data = loader.Load(id, fileSystem, isCompiled);
    ASSERT_NE(data, nullptr);
    ASSERT_FALSE(isCompiled);

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

    bool isCompiled = false;
    AssetId id = AssetId::Of<ImageAsset>("engine://Image/Test/rusty_metal_04_rough_2k.exr");
    EXPECT_EQ(loader.Load(id, fileSystem, isCompiled), nullptr);
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

// A .ktx2 is the already-compiled form: Load hands back finished data and the compile
// stage is skipped entirely. The assertions that actually prove the skip are the mip
// count and the format -- the default image descriptor asks for a full BCn/sRGB chain,
// so running the compiler would produce 8 mips of BC3_UNORM_SRGB instead.
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
