#include <gtest/gtest.h>

#include <Resource/AssetManager.h>
#include <Resource/Image/ImageAsset.h>

using namespace Spark;
using namespace Spark::Resource;

class ImageAssetTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
        m_assetManager->AddSearchPath(IMAGE_ASSET_DIR);
    }

    void TearDown() override
    {
        m_assetManager.reset();
    }

    SystemUniquePtr<SparkAssetManager> m_assetManager;
};

// ---- ImageAssetData 单元测试 ----

TEST(ImageAssetDataTest, BytesPerPixelR8)
{
    eastl::vector<uint8_t> pixels(16);
    ImageAssetData data(4, 4, ImageFormat::R8, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 1u);
    EXPECT_FALSE(data.IsHDR());
}

TEST(ImageAssetDataTest, BytesPerPixelRG8)
{
    eastl::vector<uint8_t> pixels(32);
    ImageAssetData data(4, 4, ImageFormat::RG8, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 2u);
    EXPECT_FALSE(data.IsHDR());
}

TEST(ImageAssetDataTest, BytesPerPixelRGBA8)
{
    eastl::vector<uint8_t> pixels(64);
    ImageAssetData data(4, 4, ImageFormat::RGBA8, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 4u);
    EXPECT_FALSE(data.IsHDR());
}

TEST(ImageAssetDataTest, BytesPerPixelRGBAF32)
{
    eastl::vector<uint8_t> pixels(256);
    ImageAssetData data(4, 4, ImageFormat::RGBAF32, eastl::move(pixels), "");
    EXPECT_EQ(data.GetBytesPerPixel(), 16u);
    EXPECT_TRUE(data.IsHDR());
}

// ---- ImageAssetLoader 测试 ----

TEST(ImageAssetLoaderTest, LoadMissingFileReturnsNull)
{
    eastl::vector<eastl::string> searchPaths = { IMAGE_ASSET_DIR };

    ImageAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("non_existent.png");
    EXPECT_EQ(loader.Load(id), nullptr);
}

TEST(ImageAssetLoaderTest, LoadJpegAsRGBA8)
{
    // JPEG 是 3 通道 RGB，Loader 应将其升级为 RGBA8
    eastl::vector<eastl::string> searchPaths = { IMAGE_ASSET_DIR };

    ImageAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("rusty_metal_04_diff_2k.jpg");
    auto data = loader.Load(id);
    ASSERT_NE(data, nullptr);

    auto* imgData = static_cast<ImageAssetData*>(data.get());
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::RGBA8);
    EXPECT_EQ(imgData->GetWidth(), 2048);
    EXPECT_EQ(imgData->GetHeight(), 2048);

    const size_t expectedBytes = static_cast<size_t>(imgData->GetWidth())
                               * imgData->GetHeight() * imgData->GetBytesPerPixel();
    EXPECT_EQ(imgData->GetPixels().size(), expectedBytes);
    EXPECT_FALSE(imgData->GetResolvedPath().empty());
}

TEST(ImageAssetLoaderTest, LoadAoJpegAsR8)
{
    // AO 贴图是单通道灰度 JPEG，预期 R8
    eastl::vector<eastl::string> searchPaths = { IMAGE_ASSET_DIR };

    ImageAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("rusty_metal_04_ao_2k.jpg");
    auto data = loader.Load(id);
    ASSERT_NE(data, nullptr);

    auto* imgData = static_cast<ImageAssetData*>(data.get());
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::R8);
    EXPECT_EQ(imgData->GetWidth(), 2048);
    EXPECT_EQ(imgData->GetHeight(), 2048);
}

TEST(ImageAssetLoaderTest, LoadDisplacementPng)
{
    // Displacement 贴图通常是单通道灰度图，预期 R8
    eastl::vector<eastl::string> searchPaths = { IMAGE_ASSET_DIR };

    ImageAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("rusty_metal_04_disp_2k.png");
    auto data = loader.Load(id);
    ASSERT_NE(data, nullptr);

    auto* imgData = static_cast<ImageAssetData*>(data.get());
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::R8);
    EXPECT_GT(imgData->GetWidth(), 0);
    EXPECT_GT(imgData->GetHeight(), 0);

    const size_t expectedBytes = static_cast<size_t>(imgData->GetWidth())
                               * imgData->GetHeight() * imgData->GetBytesPerPixel();
    EXPECT_EQ(imgData->GetPixels().size(), expectedBytes);
}

TEST(ImageAssetLoaderTest, ExrNotSupportedReturnsNull)
{
    eastl::vector<eastl::string> searchPaths = { IMAGE_ASSET_DIR };

    ImageAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("rusty_metal_04_rough_2k.exr");
    EXPECT_EQ(loader.Load(id), nullptr);
}

// ---- AssetManager 集成测试 ----

TEST_F(ImageAssetTestFixture, LoadImageAssetSync)
{
    m_assetManager->RegisterAssetLoader(
        eastl::make_unique<ImageAssetLoader>(),
        AssetType::Image);

    AssetId id("rusty_metal_04_diff_2k.jpg");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Image);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsReady());
    EXPECT_EQ(asset->GetAssetType(), AssetType::Image);

    auto* imgData = asset->GetData<ImageAssetData>();
    ASSERT_NE(imgData, nullptr);
    EXPECT_EQ(imgData->GetFormat(), ImageFormat::RGBA8);
    EXPECT_GT(imgData->GetWidth(), 0);
    EXPECT_GT(imgData->GetHeight(), 0);
}

TEST_F(ImageAssetTestFixture, LoadSameImageReturnsCached)
{
    m_assetManager->RegisterAssetLoader(
        eastl::make_unique<ImageAssetLoader>(),
        AssetType::Image);

    AssetId id("rusty_metal_04_diff_2k.jpg");
    Ptr<Asset> asset1 = m_assetManager->LoadAsset(id, AssetType::Image);
    Ptr<Asset> asset2 = m_assetManager->LoadAsset(id, AssetType::Image);

    ASSERT_NE(asset1, nullptr);
    EXPECT_EQ(asset1.get(), asset2.get());
}

TEST_F(ImageAssetTestFixture, LoadNonExistentImageReturnsError)
{
    m_assetManager->RegisterAssetLoader(
        eastl::make_unique<ImageAssetLoader>(),
        AssetType::Image);

    AssetId id("non_existent.png");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Image);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsError());
}
