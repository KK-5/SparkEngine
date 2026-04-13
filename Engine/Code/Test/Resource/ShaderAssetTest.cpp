#include <gtest/gtest.h>

#include <Resource/Asset.h>
#include <Resource/AssetManager.h>
#include <Resource/Common/CommonAssetLoader.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderAssetCompiler.h>

using namespace Spark;
using namespace Spark::Resource;

// Loader 需要的搜索路径列表，生命周期覆盖整个 fixture
static eastl::vector<eastl::string> s_searchPaths = { SHADER_ASSET_DIR };

class ShaderAssetTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();

        m_assetManager->AddSearchPath(SHADER_ASSET_DIR);
    }

    void TearDown() override
    {
        m_assetManager.reset();
    }

    SystemUniquePtr<SparkAssetManager> m_assetManager;
};

TEST(AssetIdTest, DefaultConstructedIsInvalid)
{
    AssetId id;
    EXPECT_FALSE(id.IsValid());
}

TEST(AssetIdTest, ConstructFromName)
{
    AssetId id("Shaders/Test/SimpleTriangle.hlsl");
    EXPECT_TRUE(id.IsValid());
    EXPECT_NE(id.GetHash(), 0u);
}

TEST(AssetIdTest, SameNameProducesSameHash)
{
    AssetId id1("Shaders/Test/SimpleTriangle.hlsl");
    AssetId id2("Shaders/Test/SimpleTriangle.hlsl");
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id1.GetHash(), id2.GetHash());
}

TEST(AssetIdTest, DifferentNameProducesDifferentHash)
{
    AssetId id1("Shaders/A.hlsl");
    AssetId id2("Shaders/B.hlsl");
    EXPECT_NE(id1, id2);
}

TEST(AssetIdTest, SubAssetHashDiffers)
{
    AssetId id1("Shaders/Test/SimpleTriangle.hlsl");
    AssetId id2("Shaders/Test/SimpleTriangle.hlsl", "VSMain");
    EXPECT_NE(id1, id2);
}


TEST(ShaderAssetDataTest, AddAndQueryStage)
{
    ShaderAssetData data;

    ShaderStageBytecode vs;
    vs.stage = RHI::ShaderStage::Vertex;
    vs.entryPoint = "VSMain";
    vs.bytecode = {0x01, 0x02, 0x03};

    data.AddStageBytecode(eastl::move(vs));

    EXPECT_TRUE(data.HasStage(RHI::ShaderStage::Vertex));
    EXPECT_FALSE(data.HasStage(RHI::ShaderStage::Fragment));

    auto* result = data.GetStageBytecode(RHI::ShaderStage::Vertex);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->entryPoint.c_str(), "VSMain");
    EXPECT_EQ(result->bytecode.size(), 3u);
}

TEST(ShaderAssetDataTest, QueryNonExistentStageReturnsNull)
{
    ShaderAssetData data;
    EXPECT_EQ(data.GetStageBytecode(RHI::ShaderStage::Compute), nullptr);
    EXPECT_FALSE(data.HasStage(RHI::ShaderStage::Compute));
}

TEST(ShaderAssetDataTest, BackendDefaultIsDXIL)
{
    ShaderAssetData data;
    EXPECT_EQ(data.GetBackend(), ShaderBackend::DXIL);
}

TEST_F(ShaderAssetTestFixture, BinaryLoaderLoadsFile)
{
    eastl::vector<eastl::string> searchPaths;
    searchPaths.push_back(SHADER_ASSET_DIR);

    BinaryAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("Shaders/Test/SimpleTriangle.hlsl");
    auto data = loader.Load(id);
    ASSERT_NE(data, nullptr);

    auto* binaryData = static_cast<BinaryAssetData*>(data.get());
    EXPECT_GT(binaryData->GetBytes().size(), 0u);
    EXPECT_FALSE(binaryData->GetResolvedPath().empty());
}

TEST_F(ShaderAssetTestFixture, BinaryLoaderReturnsNullForMissing)
{
    eastl::vector<eastl::string> searchPaths;
    searchPaths.push_back(SHADER_ASSET_DIR);

    BinaryAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id("Shaders/NonExistent.hlsl");
    auto data = loader.Load(id);
    EXPECT_EQ(data, nullptr);
}

TEST_F(ShaderAssetTestFixture, CompileHLSLToDXIL)
{
    // 先用 Loader 读取源文件
    eastl::vector<eastl::string> searchPaths;
    searchPaths.push_back(SHADER_ASSET_DIR);

    BinaryAssetLoader loader;
    loader.SetSearchPaths(searchPaths);
    AssetId id("Shaders/Test/SimpleTriangle.hlsl");
    auto rawData = loader.Load(id);
    ASSERT_NE(rawData, nullptr);

    // 用 Compiler 编译
    ShaderAssetCompiler compiler(ShaderBackend::DXIL);
    compiler.AddStageEntry({RHI::ShaderStage::Vertex, "VSMain", "vs_6_0"});
    compiler.AddStageEntry({RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"});

    auto compiledData = compiler.Compile(id, *rawData);
    ASSERT_NE(compiledData, nullptr);

    auto* shaderData = static_cast<ShaderAssetData*>(compiledData.get());
    EXPECT_EQ(shaderData->GetBackend(), ShaderBackend::DXIL);

    // 验证 VS
    EXPECT_TRUE(shaderData->HasStage(RHI::ShaderStage::Vertex));
    auto* vsBytecode = shaderData->GetStageBytecode(RHI::ShaderStage::Vertex);
    ASSERT_NE(vsBytecode, nullptr);
    EXPECT_STREQ(vsBytecode->entryPoint.c_str(), "VSMain");
    EXPECT_GT(vsBytecode->bytecode.size(), 0u);

    // 验证 PS
    EXPECT_TRUE(shaderData->HasStage(RHI::ShaderStage::Fragment));
    auto* psBytecode = shaderData->GetStageBytecode(RHI::ShaderStage::Fragment);
    ASSERT_NE(psBytecode, nullptr);
    EXPECT_STREQ(psBytecode->entryPoint.c_str(), "PSMain");
    EXPECT_GT(psBytecode->bytecode.size(), 0u);
}

TEST_F(ShaderAssetTestFixture, LoadShaderAssetSync)
{
    m_assetManager->RegisterAssetLoader(
        eastl::make_unique<BinaryAssetLoader>(),
        AssetType::Shader);

    auto compiler = eastl::make_unique<ShaderAssetCompiler>(ShaderBackend::DXIL);
    compiler->AddStageEntry({RHI::ShaderStage::Vertex, "VSMain", "vs_6_0"});
    compiler->AddStageEntry({RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"});
    m_assetManager->RegisterAssetCompiler(eastl::move(compiler), AssetType::Shader);

    AssetId id("Shaders/Test/SimpleTriangle.hlsl");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Shader);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsReady());

    auto* shaderData = asset->GetData<ShaderAssetData>();
    ASSERT_NE(shaderData, nullptr);
    EXPECT_TRUE(shaderData->HasStage(RHI::ShaderStage::Vertex));
    EXPECT_TRUE(shaderData->HasStage(RHI::ShaderStage::Fragment));
}

TEST_F(ShaderAssetTestFixture, LoadSameAssetReturnsCached)
{
    m_assetManager->RegisterAssetLoader(
        eastl::make_unique<BinaryAssetLoader>(),
        AssetType::Shader);

    auto compiler = eastl::make_unique<ShaderAssetCompiler>(ShaderBackend::DXIL);
    compiler->AddStageEntry({RHI::ShaderStage::Vertex, "VSMain", "vs_6_0"});
    m_assetManager->RegisterAssetCompiler(eastl::move(compiler), AssetType::Shader);

    AssetId id("Shaders/Test/SimpleTriangle.hlsl");
    Ptr<Asset> asset1 = m_assetManager->LoadAsset(id, AssetType::Shader);
    Ptr<Asset> asset2 = m_assetManager->LoadAsset(id, AssetType::Shader);

    EXPECT_EQ(asset1.get(), asset2.get());
}

TEST_F(ShaderAssetTestFixture, FindAssetBeforeLoadReturnsNull)
{
    AssetId id("Shaders/Test/SimpleTriangle.hlsl");
    Ptr<Asset> asset = m_assetManager->FindAsset(id, AssetType::Shader);
    EXPECT_EQ(asset, nullptr);
}

TEST_F(ShaderAssetTestFixture, LoadNonExistentAssetReturnsError)
{
    m_assetManager->RegisterAssetLoader(
        eastl::make_unique<BinaryAssetLoader>(),
        AssetType::Shader);

    AssetId id("Shaders/NonExistent.hlsl");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Shader);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsError());
}
