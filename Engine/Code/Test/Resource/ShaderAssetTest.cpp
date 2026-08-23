#include <gtest/gtest.h>

#include <Resource/Asset.h>
#include <Resource/AssetManager.h>
#include <VFS/MountTable.h>
#include <VFS/VFSSystem.h>
#include <Resource/Common/CommonAssetLoader.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderAssetCompiler.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>
#include <Resource/Shader/ShaderBuilder.h>

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

class ShaderAssetTestFixture : public ::testing::Test
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

    SystemUniquePtr<VFSSystem>           m_vfs;
    SystemUniquePtr<SparkAssetManager>   m_assetManager;
};

TEST(AssetIdTest, DefaultConstructedIsInvalid)
{
    AssetId id;
    EXPECT_FALSE(id.IsValid());
}

TEST(AssetIdTest, ConstructFromName)
{
    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    EXPECT_TRUE(id.IsValid());
    EXPECT_NE(id.GetHash(), 0u);
}

TEST(AssetIdTest, SameNameProducesSameHash)
{
    AssetId id1 = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    AssetId id2 = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id1.GetHash(), id2.GetHash());
}

TEST(AssetIdTest, DifferentNameProducesDifferentHash)
{
    AssetId id1 = AssetId::Of<ShaderAsset>("engine://Shaders/A.hlsl");
    AssetId id2 = AssetId::Of<ShaderAsset>("engine://Shaders/B.hlsl");
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

TEST(ShaderAssetDataTest, AddAndQueryStageReflection)
{
    ShaderAssetData data;

    ShaderStageReflection refl;
    ShaderResourceBindingReflection res;
    res.m_name = "g_Texture";
    res.m_type = RHI::ShaderInputType::Image;
    res.m_registerId = 0;
    refl.m_resources.push_back(res);

    ShaderConstantBufferReflection cb;
    cb.m_name = "MyCB";
    cb.m_registerId = 0;
    refl.m_cbuffers.push_back(cb);

    data.AddStageReflection(RHI::ShaderStage::Fragment, eastl::move(refl));

    EXPECT_TRUE(data.HasStageReflection(RHI::ShaderStage::Fragment));
    EXPECT_FALSE(data.HasStageReflection(RHI::ShaderStage::Vertex));

    auto* result = data.GetStageReflection(RHI::ShaderStage::Fragment);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->m_resources.size(), 1u);
    EXPECT_STREQ(result->m_resources[0].m_name.c_str(), "g_Texture");
    EXPECT_EQ(result->m_resources[0].m_type, RHI::ShaderInputType::Image);
    EXPECT_EQ(result->m_cbuffers.size(), 1u);
    EXPECT_STREQ(result->m_cbuffers[0].m_name.c_str(), "MyCB");
}

TEST(ShaderAssetDataTest, QueryNonExistentStageReflectionReturnsNull)
{
    ShaderAssetData data;
    EXPECT_EQ(data.GetStageReflection(RHI::ShaderStage::Compute), nullptr);
    EXPECT_FALSE(data.HasStageReflection(RHI::ShaderStage::Compute));
}

TEST_F(ShaderAssetTestFixture, BinaryLoaderLoadsFile)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    BinaryAssetLoader loader;

    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    auto data = loader.Load(id, fileSystem);
    ASSERT_NE(data, nullptr);

    auto* binaryData = static_cast<BinaryAssetData*>(data.get());
    EXPECT_GT(binaryData->GetBytes().size(), 0u);
    EXPECT_FALSE(binaryData->GetResolvedPath().empty());
}

TEST_F(ShaderAssetTestFixture, BinaryLoaderReturnsNullForMissing)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    BinaryAssetLoader loader;

    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/NonExistent.hlsl");
    auto data = loader.Load(id, fileSystem);
    EXPECT_EQ(data, nullptr);
}

TEST_F(ShaderAssetTestFixture, CompileHLSLToDXIL)
{
    // 先用 Loader 读取源文件
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    BinaryAssetLoader loader;
    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    auto rawData = loader.Load(id, fileSystem);
    ASSERT_NE(rawData, nullptr);

    // 用 Compiler 编译
    ShaderAssetCompiler compiler;
    ShaderDescriptor descriptor;
    descriptor.backend = ShaderBackend::DXIL;
    descriptor.stages = {
        {RHI::ShaderStage::Vertex,   "VSMain", "vs_6_0"},
        {RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"},
    };

    auto compiledData = compiler.Compile(id, *rawData, fileSystem, descriptor);
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

TEST_F(ShaderAssetTestFixture, CompileHLSLReflection)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    BinaryAssetLoader loader;
    AssetId id = AssetId::Of<ShaderAsset>("test://Asset/Shaders/ReflectionTest.hlsl");
    auto rawData = loader.Load(id, fileSystem);
    ASSERT_NE(rawData, nullptr);

    ShaderAssetCompiler compiler;
    ShaderDescriptor descriptor;
    descriptor.backend = ShaderBackend::DXIL;
    descriptor.stages = {
        {RHI::ShaderStage::Vertex,   "VSMain", "vs_6_0"},
        {RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"},
    };

    auto compiledData = compiler.Compile(id, *rawData, fileSystem, descriptor);
    ASSERT_NE(compiledData, nullptr);

    auto* shaderData = static_cast<ShaderAssetData*>(compiledData.get());

    auto findCbuffer = [](const auto& cbuffers, const char* name) -> const ShaderConstantBufferReflection* {
        for (auto& cb : cbuffers) {
            if (cb.m_name == name) return &cb;
        }
        return nullptr;
    };

    auto findCbufferVar = [](const auto& cb, const char* name) -> const ShaderConstantVariableReflection* {
        for (auto& v : cb.m_variables) {
            if (v.m_name == name) return &v;
        }
        return nullptr;
    };

    auto findResource = [](const auto& resources, const char* name) -> const ShaderResourceBindingReflection* {
        for (auto& r : resources) {
            if (r.m_name == name) return &r;
        }
        return nullptr;
    };

    // ===================================================
    // Vertex stage reflection
    // ===================================================
    ASSERT_TRUE(shaderData->HasStageReflection(RHI::ShaderStage::Vertex));
    auto* vsRefl = shaderData->GetStageReflection(RHI::ShaderStage::Vertex);
    ASSERT_NE(vsRefl, nullptr);

    // cbuffer SceneParams (referenced by VSMain via viewProj)
    auto* vsSceneCB = findCbuffer(vsRefl->m_cbuffers, "SceneParams");
    ASSERT_NE(vsSceneCB, nullptr);
    EXPECT_GE(vsSceneCB->m_byteSize, 80u);
    EXPECT_EQ(vsSceneCB->m_registerId, 0u);
    EXPECT_EQ(vsSceneCB->m_spaceId, 0u);

    // SceneParams::viewProj
    auto* viewProjVar = findCbufferVar(*vsSceneCB, "viewProj");
    ASSERT_NE(viewProjVar, nullptr);
    EXPECT_EQ(viewProjVar->m_byteOffset, 0u);
    EXPECT_EQ(viewProjVar->m_byteSize, 64u);

    // SceneParams::lightDir
    auto* lightDirVar = findCbufferVar(*vsSceneCB, "lightDir");
    ASSERT_NE(lightDirVar, nullptr);
    EXPECT_GE(lightDirVar->m_byteOffset, 64u);
    EXPECT_EQ(lightDirVar->m_byteSize, 16u);

    // SceneParams::time
    auto* timeVar = findCbufferVar(*vsSceneCB, "time");
    ASSERT_NE(timeVar, nullptr);
    EXPECT_GE(timeVar->m_byteOffset, 80u);
    EXPECT_EQ(timeVar->m_byteSize, 4u);

    // VS should NOT have g_AlbedoMap / g_AlbedoSamp (not referenced)
    EXPECT_EQ(findResource(vsRefl->m_resources, "g_AlbedoMap"), nullptr);
    EXPECT_EQ(findResource(vsRefl->m_resources, "g_AlbedoSamp"), nullptr);

    // ===================================================
    // Fragment stage reflection
    // ===================================================
    ASSERT_TRUE(shaderData->HasStageReflection(RHI::ShaderStage::Fragment));
    auto* psRefl = shaderData->GetStageReflection(RHI::ShaderStage::Fragment);
    ASSERT_NE(psRefl, nullptr);

    // Texture2D g_AlbedoMap → Image
    auto* albedoRes = findResource(psRefl->m_resources, "g_AlbedoMap");
    ASSERT_NE(albedoRes, nullptr);
    EXPECT_EQ(albedoRes->m_type, RHI::ShaderInputType::Image);
    EXPECT_EQ(albedoRes->m_registerId, 0u);
    EXPECT_EQ(albedoRes->m_spaceId, 0u);
    EXPECT_EQ(albedoRes->m_imageAccess, RHI::ShaderInputImageAccess::Read);
    EXPECT_EQ(albedoRes->m_imageType, RHI::ShaderInputImageType::Image2D);

    // SamplerState g_AlbedoSamp → Sampler
    auto* sampRes = findResource(psRefl->m_resources, "g_AlbedoSamp");
    ASSERT_NE(sampRes, nullptr);
    EXPECT_EQ(sampRes->m_type, RHI::ShaderInputType::Sampler);
    EXPECT_EQ(sampRes->m_registerId, 0u);
    EXPECT_EQ(sampRes->m_spaceId, 0u);

    // cbuffer SceneParams (referenced by PSMain via time)
    auto* psSceneCB = findCbuffer(psRefl->m_cbuffers, "SceneParams");
    ASSERT_NE(psSceneCB, nullptr);
    EXPECT_GE(psSceneCB->m_byteSize, 80u);
    auto* psTimeVar = findCbufferVar(*psSceneCB, "time");
    ASSERT_NE(psTimeVar, nullptr);
}

TEST_F(ShaderAssetTestFixture, BuildShaderInputListFromReflection)
{
    MountTable fileSystem;
    SetUpMounts(fileSystem);

    BinaryAssetLoader loader;
    AssetId id = AssetId::Of<ShaderAsset>("test://Asset/Shaders/ReflectionTest.hlsl");
    auto rawData = loader.Load(id, fileSystem);
    ASSERT_NE(rawData, nullptr);

    ShaderAssetCompiler compiler;
    ShaderDescriptor descriptor;
    descriptor.backend = ShaderBackend::DXIL;
    descriptor.stages = {
        {RHI::ShaderStage::Vertex,   "VSMain", "vs_6_0"},
        {RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"},
    };

    auto compiledData = compiler.Compile(id, *rawData, fileSystem, descriptor);
    ASSERT_NE(compiledData, nullptr);

    ShaderAsset shader(id);
    shader.SetDataReady(eastl::move(compiledData));

    auto built = Resource::BuildShaderInputList(shader);

    // ---- Buffer: cbuffer 不产生 Buffer(CBV) 描述符 ----
    EXPECT_EQ(built.list.m_buffers.size(), 0u);

    // ---- Image: g_AlbedoMap (t0, space0) ----
    {
        ASSERT_EQ(built.list.m_images.size(), 1u);
        const auto& img = built.list.m_images[0];
        EXPECT_STREQ(img.m_name.GetCStr(), "g_AlbedoMap");
        EXPECT_EQ(img.m_access, RHI::ShaderInputImageAccess::Read);
        EXPECT_EQ(img.m_type, RHI::ShaderInputImageType::Image2D);
        EXPECT_EQ(img.m_registerId, 0u);
        EXPECT_EQ(img.m_spaceId, 0u);
    }

    // ---- Sampler: g_AlbedoSamp (s0, space0) ----
    {
        ASSERT_EQ(built.list.m_samplers.size(), 1u);
        const auto& smp = built.list.m_samplers[0];
        EXPECT_STREQ(smp.m_name.GetCStr(), "g_AlbedoSamp");
        EXPECT_EQ(smp.m_registerId, 0u);
        EXPECT_EQ(smp.m_spaceId, 0u);
    }

    // ---- Constants: cbuffer 变量展开为 Constant 描述符 ----
    {
        const auto& constants = built.list.m_constants;
        ASSERT_EQ(constants.size(), 3u);

        auto findConst = [&](const char* name) -> const RHI::ShaderInputConstantDescriptor* {
            for (auto& c : constants) {
                if (eastl::string_view(c.m_name.GetCStr()) == name) return &c;
            }
            return nullptr;
        };

        auto* viewProj = findConst("viewProj");
        ASSERT_NE(viewProj, nullptr);
        EXPECT_EQ(viewProj->m_constantByteOffset, 0u);
        EXPECT_EQ(viewProj->m_constantByteCount, 64u);

        auto* lightDir = findConst("lightDir");
        ASSERT_NE(lightDir, nullptr);
        EXPECT_GE(lightDir->m_constantByteOffset, 64u);
        EXPECT_EQ(lightDir->m_constantByteCount, 16u);

        auto* time = findConst("time");
        ASSERT_NE(time, nullptr);
        EXPECT_GE(time->m_constantByteOffset, 80u);
        EXPECT_EQ(time->m_constantByteCount, 4u);
    }
}

TEST_F(ShaderAssetTestFixture, LoadShaderAssetSync)
{
    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
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
    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    Ptr<Asset> asset1 = m_assetManager->LoadAsset(id, AssetType::Shader);
    Ptr<Asset> asset2 = m_assetManager->LoadAsset(id, AssetType::Shader);

    EXPECT_EQ(asset1.get(), asset2.get());
}

TEST_F(ShaderAssetTestFixture, FindAssetBeforeLoadReturnsNull)
{
    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/Test/SimpleTriangle.hlsl");
    Ptr<Asset> asset = m_assetManager->FindAsset(id);
    EXPECT_EQ(asset, nullptr);
}

TEST_F(ShaderAssetTestFixture, LoadNonExistentAssetReturnsError)
{
    AssetId id = AssetId::Of<ShaderAsset>("engine://Shaders/NonExistent.hlsl");
    Ptr<Asset> asset = m_assetManager->LoadAsset(id, AssetType::Shader);

    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->IsError());
}
