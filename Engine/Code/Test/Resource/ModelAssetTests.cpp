#include <gtest/gtest.h>

#include <Resource/AssetManager.h>
#include <VFS/MountTable.h>
#include <VFS/VFSSystem.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Model/ModelAssetLoader.h>
#include <Resource/Model/ModelAssetCompiler.h>

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

class ModelAssetTestFixture : public ::testing::Test
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

    SystemUniquePtr<VFSSystem>         m_vfs;
    SystemUniquePtr<SparkAssetManager> m_assetManager;
};

// ===== Loader unit tests =====

TEST(ModelAssetLoaderTest, LoadMissingFileReturnsNull)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ModelAssetLoader loader;

    AssetId id = AssetId::Of<ModelAsset>("test://Asset/non_existent.glb");
    EXPECT_EQ(loader.Load(id, fileSystem), nullptr);
}

TEST(ModelAssetLoaderTest, LoadCubeGLB)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ModelAssetLoader loader;

    AssetId id = AssetId::Of<ModelAsset>("test://Asset/Cube.glb");
    auto data = loader.Load(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* modelData = static_cast<ModelAssetRawData*>(data.get());
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
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ModelAssetLoader loader;

    AssetId id = AssetId::Of<ModelAsset>("test://Asset/Cube.glb");
    auto data = loader.Load(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* modelData = static_cast<ModelAssetRawData*>(data.get());
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
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ModelAssetLoader loader;

    AssetId id = AssetId::Of<ModelAsset>("test://Asset/Cube.glb");
    auto rawData = loader.Load(id, fileSystem);
    ASSERT_NE(rawData, nullptr);

    auto* rawModel = static_cast<ModelAssetRawData*>(rawData.get());
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
    AssetId id = AssetId::Of<ModelAsset>("test://Asset/Cube.glb");
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
    AssetId id = AssetId::Of<ModelAsset>("test://Asset/Cube.glb");
    Ptr<Asset> asset1 = m_assetManager->LoadAsset(id, AssetType::Model);
    Ptr<Asset> asset2 = m_assetManager->LoadAsset(id, AssetType::Model);

    ASSERT_NE(asset1, nullptr);
    EXPECT_EQ(asset1.get(), asset2.get());
}

TEST_F(ModelAssetTestFixture, LoadNonExistentModelReturnsError)
{
    AssetId id = AssetId::Of<ModelAsset>("test://Asset/non_existent.glb");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Model);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsError());
}

TEST_F(ModelAssetTestFixture, FindAssetBeforeLoadReturnsNull)
{
    AssetId id = AssetId::Of<ModelAsset>("test://Asset/Cube.glb");
    Ptr<Asset> asset = m_assetManager->FindAsset(id);
    EXPECT_EQ(asset, nullptr);
}

// ===== Embedded image (.glb) — Loader 层 =====

TEST(ModelAssetLoaderTest, LoadCubeTexturedGLB_HasEmbeddedImage)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ModelAssetLoader loader;

    AssetId id = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb");
    auto data = loader.Load(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* model = static_cast<ModelAssetRawData*>(data.get());
    ASSERT_EQ(model->GetRawImageCount(), 1u);

    const RawImageEntry* entry = model->GetRawImage(0);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->data.empty());         // 内嵌：data 非空
    EXPECT_TRUE(entry->externalUri.empty());   // 内嵌：URI 空
    EXPECT_GT(entry->data.size(), 1024u);      // 至少有压缩图字节
}

// ===== External image (.gltf) — Loader 层 =====

TEST(ModelAssetLoaderTest, LoadCubeTexturedGLTF_HasExternalImage)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    ModelAssetLoader loader;

    AssetId id = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.gltf");
    auto data = loader.Load(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* model = static_cast<ModelAssetRawData*>(data.get());
    ASSERT_EQ(model->GetRawImageCount(), 1u);

    const RawImageEntry* entry = model->GetRawImage(0);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->data.empty());                                    // 外部：data 空
    EXPECT_EQ(entry->externalUri, "Textures/stone_wall_04_diff_1k.jpg"); // 外部：URI 是相对路径
    EXPECT_EQ(entry->name, "stone_wall_04_diff_1k");
}

// ===== Embedded image (.glb) — Builder 派发 + m_imageAssetIds =====

TEST_F(ModelAssetTestFixture, LoadCubeTexturedGLB_DispatchesEmbeddedImage)
{
    AssetId modelId = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb");
    Ptr<Asset> modelAsset = m_assetManager->LoadAsset(modelId, AssetType::Model);

    ASSERT_NE(modelAsset, nullptr);
    EXPECT_TRUE(modelAsset->IsReady());

    auto* modelData = modelAsset->GetData<ModelAssetData>();
    ASSERT_NE(modelData, nullptr);

    // m_imageAssetIds 跟 gltf.images[] 对齐
    ASSERT_EQ(modelData->GetImageAssetCount(), 1u);
    const AssetId& imgId = modelData->GetImageAssetId(0);
    EXPECT_TRUE(imgId.IsValid());

    // 子资产应该被注册到 db 且 Ready
    Ptr<Asset> imgAsset = m_assetManager->FindAsset(imgId);
    ASSERT_NE(imgAsset, nullptr);
    EXPECT_EQ(imgAsset->GetAssetType(), AssetType::Image);
    EXPECT_TRUE(imgAsset->IsReady());

    // 内嵌图的 subId 形态：(parentPath, "image/<index>/<name>")。索引恒在前是为了去重
    // ——glTF 允许多张图重名（或都为空名），只靠 name 会塌成同一个 AssetId。
    EXPECT_EQ(imgId.GetPath(), "test://Asset/CubeTextured.glb");
    EXPECT_EQ(imgId.GetSubLabel(), "image/0/stone_wall_04_diff_1k");
}

// ===== External image (.gltf) — Builder 派发 + m_imageAssetIds =====

TEST_F(ModelAssetTestFixture, LoadCubeTexturedGLTF_DispatchesExternalImage)
{
    AssetId modelId = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.gltf");
    Ptr<Asset> modelAsset = m_assetManager->LoadAsset(modelId, AssetType::Model);

    ASSERT_NE(modelAsset, nullptr);
    EXPECT_TRUE(modelAsset->IsReady());

    auto* modelData = modelAsset->GetData<ModelAssetData>();
    ASSERT_NE(modelData, nullptr);

    ASSERT_EQ(modelData->GetImageAssetCount(), 1u);
    const AssetId& imgId = modelData->GetImageAssetId(0);
    EXPECT_TRUE(imgId.IsValid());

    // 外部图不是 sub，用的是把 glTF 相对 URI 词法解析到父模型虚拟目录下的结果
    EXPECT_EQ(imgId.GetPath(), "test://Asset/Textures/stone_wall_04_diff_1k.jpg");
    EXPECT_FALSE(imgId.IsSubAsset());

    Ptr<Asset> imgAsset = m_assetManager->FindAsset(imgId);
    ASSERT_NE(imgAsset, nullptr);
    EXPECT_EQ(imgAsset->GetAssetType(), AssetType::Image);
    EXPECT_TRUE(imgAsset->IsReady());
}

// ===== 编译资产携带解析后的内嵌材质 =====
// （raw 图字节不在编译资产上，现在由类型保证：ModelAssetData 根本没有 raw 图字段，
//   只有 ModelAssetRawData 才有——所以旧的运行时断言退化成编译期保证，这里改测材质。）

TEST_F(ModelAssetTestFixture, CompiledModelCarriesResolvedMaterials)
{
    AssetId modelId = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.glb");
    Ptr<Asset> modelAsset = m_assetManager->LoadAsset(modelId, AssetType::Model);
    ASSERT_NE(modelAsset, nullptr);

    auto* modelData = modelAsset->GetData<ModelAssetData>();
    ASSERT_NE(modelData, nullptr);

    // 内嵌材质被填充，且 base-color 贴图引用解析到了已派发的 image 子资产。
    ASSERT_GE(modelData->GetMaterialCount(), 1u);
    ASSERT_GE(modelData->GetImageAssetCount(), 1u);
    const Material* mat = modelData->GetMaterial(0);
    ASSERT_NE(mat, nullptr);
    EXPECT_TRUE(mat->baseColorImageId.IsValid());
    EXPECT_EQ(mat->baseColorImageId, modelData->GetImageAssetId(0));
}

// ===== 同一外部图被多次加载应该 dedup =====

TEST_F(ModelAssetTestFixture, ExternalImageDedupAcrossModelLoads)
{
    AssetId modelId = AssetId::Of<ModelAsset>("test://Asset/CubeTextured.gltf");

    Ptr<Asset> first  = m_assetManager->LoadAsset(modelId, AssetType::Model);
    Ptr<Asset> second = m_assetManager->LoadAsset(modelId, AssetType::Model);
    EXPECT_EQ(first.get(), second.get());  // model 本身已 dedup

    auto* modelData = first->GetData<ModelAssetData>();
    const AssetId& imgId = modelData->GetImageAssetId(0);

    Ptr<Asset> imgA = m_assetManager->FindAsset(imgId);
    Ptr<Asset> imgB = m_assetManager->FindAsset(imgId);
    EXPECT_EQ(imgA.get(), imgB.get());     // image 也指向同一实例
}
