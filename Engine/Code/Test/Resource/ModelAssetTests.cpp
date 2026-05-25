#include <gtest/gtest.h>

#include <Resource/AssetManager.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Model/ModelAssetLoader.h>
#include <Resource/Model/ModelAssetCompiler.h>

using namespace Spark;
using namespace Spark::Resource;

class ModelAssetTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_assetManager = CreateSystem<SparkAssetManager>();
        m_assetManager->Init();
        m_assetManager->AddSearchPath(MODEL_ASSET_DIR);
    }

    void TearDown() override
    {
        m_assetManager.reset();
    }

    SystemUniquePtr<SparkAssetManager> m_assetManager;
};

// ===== Loader unit tests =====

TEST(ModelAssetLoaderTest, LoadMissingFileReturnsNull)
{
    eastl::vector<eastl::string> searchPaths = { MODEL_ASSET_DIR };

    ModelAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id = AssetId::Of<ModelAsset>("non_existent.glb");
    EXPECT_EQ(loader.Load(id), nullptr);
}

TEST(ModelAssetLoaderTest, LoadCubeGLB)
{
    eastl::vector<eastl::string> searchPaths = { MODEL_ASSET_DIR };

    ModelAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id = AssetId::Of<ModelAsset>("Cube.glb");
    auto data = loader.Load(id);
    ASSERT_NE(data, nullptr);

    auto* modelData = static_cast<ModelAssetData*>(data.get());
    EXPECT_FALSE(modelData->GetResolvedPath().empty());

    // Cube 至少有 1 个 mesh
    EXPECT_GE(modelData->GetMeshCount(), 1u);

    // 第一个 mesh 有名字且有至少 1 个 primitive
    const Mesh* mesh = modelData->GetMesh(0);
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->name.empty());
    EXPECT_GE(mesh->primitives.size(), 1u);

    // Primitive 有顶点和索引数据
    const Primitive& prim = mesh->primitives[0];
    EXPECT_GT(prim.vertexBuffer.size(), 0u);
    EXPECT_GT(prim.indexBuffer.size(), 0u);

    // 索引数量应该是 3 的倍数（triangle list）
    const size_t indexCount = prim.indexBuffer.size() / sizeof(uint32_t);
    EXPECT_EQ(indexCount % 3, 0u);
    EXPECT_GE(indexCount, 3u);

    // 应有 POSITION attribute
    bool hasPosition = false;
    for (const auto& attr : prim.layout.attributes)
    {
        if (attr.semantic == "POSITION")
        {
            hasPosition = true;
            EXPECT_EQ(attr.format, RHI::Format::R32G32B32_FLOAT);
            break;
        }
    }
    EXPECT_TRUE(hasPosition);

    // Vertex buffer 大小应等于 vertexCount * stride
    const size_t vertexCount = prim.vertexBuffer.size() / prim.layout.stride;
    EXPECT_EQ(prim.vertexBuffer.size(), vertexCount * prim.layout.stride);
    EXPECT_GT(vertexCount, 0u);

    // 索引值应在合法范围内
    const uint32_t* indices = reinterpret_cast<const uint32_t*>(prim.indexBuffer.data());
    for (size_t i = 0; i < indexCount; ++i)
    {
        EXPECT_LT(indices[i], static_cast<uint32_t>(vertexCount));
    }

    // Global bounds 应有效
    const Math::AABB& bounds = modelData->GetBounds();
    EXPECT_LE(bounds.min.x, bounds.max.x);
    EXPECT_LE(bounds.min.y, bounds.max.y);
    EXPECT_LE(bounds.min.z, bounds.max.z);
}

TEST(ModelAssetLoaderTest, LoadCubeGLBHasNodes)
{
    eastl::vector<eastl::string> searchPaths = { MODEL_ASSET_DIR };

    ModelAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id = AssetId::Of<ModelAsset>("Cube.glb");
    auto data = loader.Load(id);
    ASSERT_NE(data, nullptr);

    auto* modelData = static_cast<ModelAssetData*>(data.get());
    EXPECT_GT(modelData->GetNodeCount(), 0u);

    // Root node 应该 parent == -1
    const Node* root = modelData->GetNode(0);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->parent, -1);
    EXPECT_FALSE(root->name.empty());

    // 根节点应该有 mesh 引用
    EXPECT_GE(root->meshIndex, 0);
}

// ===== Compiler unit tests =====

TEST(ModelAssetCompilerTest, CompileCube)
{
    eastl::vector<eastl::string> searchPaths = { MODEL_ASSET_DIR };

    ModelAssetLoader loader;
    loader.SetSearchPaths(searchPaths);

    AssetId id = AssetId::Of<ModelAsset>("Cube.glb");
    auto rawData = loader.Load(id);
    ASSERT_NE(rawData, nullptr);

    auto* rawModel = static_cast<ModelAssetData*>(rawData.get());
    const size_t meshCountBefore  = rawModel->GetMeshCount();
    size_t primCountBefore = 0;
    for (size_t m = 0; m < meshCountBefore; ++m)
    {
        primCountBefore += rawModel->GetMesh(m)->primitives.size();
    }

    ModelAssetCompiler compiler;
    auto compiledData = compiler.Compile(id, *rawData);
    ASSERT_NE(compiledData, nullptr);

    auto* modelData = static_cast<ModelAssetData*>(compiledData.get());
    EXPECT_EQ(modelData->GetMeshCount(), meshCountBefore);

    // Primitive 数量应保持不变
    size_t primCountAfter = 0;
    for (size_t m = 0; m < modelData->GetMeshCount(); ++m)
    {
        primCountAfter += modelData->GetMesh(m)->primitives.size();
    }
    EXPECT_EQ(primCountAfter, primCountBefore);

    // 编译后 vertex/index buffer 仍存在
    const Mesh* mesh = modelData->GetMesh(0);
    ASSERT_NE(mesh, nullptr);
    ASSERT_GE(mesh->primitives.size(), 1u);

    const Primitive& prim = mesh->primitives[0];
    EXPECT_GT(prim.vertexBuffer.size(), 0u);
    EXPECT_GT(prim.indexBuffer.size(), 0u);

    // 优化后 vertex 数量不增（meshoptimizer 可能移除 unused vertices）
    const size_t idxCount = prim.indexBuffer.size() / sizeof(uint32_t);
    const size_t vtxCount = prim.vertexBuffer.size() / prim.layout.stride;
    EXPECT_GE(idxCount, 3u);
    EXPECT_GT(vtxCount, 0u);

    // 索引仍在合法范围内
    const uint32_t* indices = reinterpret_cast<const uint32_t*>(prim.indexBuffer.data());
    for (size_t i = 0; i < idxCount; ++i)
    {
        EXPECT_LT(indices[i], static_cast<uint32_t>(vtxCount));
    }
}

// ===== Full pipeline tests (AssetManager) =====

TEST_F(ModelAssetTestFixture, LoadModelAssetSync)
{
    AssetId id = AssetId::Of<ModelAsset>("Cube.glb");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Model);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsReady());
    EXPECT_EQ(asset->GetAssetType(), AssetType::Model);

    auto* modelData = asset->GetData<ModelAssetData>();
    ASSERT_NE(modelData, nullptr);

    EXPECT_GE(modelData->GetMeshCount(), 1u);
    EXPECT_FALSE(modelData->GetResolvedPath().empty());
}

TEST_F(ModelAssetTestFixture, LoadSameModelReturnsCached)
{
    AssetId id = AssetId::Of<ModelAsset>("Cube.glb");
    Ptr<Asset> asset1 = m_assetManager->LoadAsset(id, AssetType::Model);
    Ptr<Asset> asset2 = m_assetManager->LoadAsset(id, AssetType::Model);

    ASSERT_NE(asset1, nullptr);
    EXPECT_EQ(asset1.get(), asset2.get());
}

TEST_F(ModelAssetTestFixture, LoadNonExistentModelReturnsError)
{
    AssetId id = AssetId::Of<ModelAsset>("non_existent.glb");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Model);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsError());
}

TEST_F(ModelAssetTestFixture, FindAssetBeforeLoadReturnsNull)
{
    AssetId id = AssetId::Of<ModelAsset>("Cube.glb");
    Ptr<Asset> asset = m_assetManager->FindAsset(id);
    EXPECT_EQ(asset, nullptr);
}
