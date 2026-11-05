#include <gtest/gtest.h>

#include <Scene/SceneTextureSystem.h>
#include <Binding/Scene/SceneBinding.h>
#include <Skybox/SkyboxSystem.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/System/RHIHandleClearSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAssetCompiler.h>

#include "FakeDevice.h"
#include "FakeHostBufferFactory.h"

using namespace Spark;
using namespace Spark::Render;

namespace
{
    class SceneImage final : public RHI::Image {};

    class SceneImagePool final : public RHI::ImagePool
    {
        RHI::ResultCode InitInternal(RHI::Device&, const RHI::ImagePoolDescriptor&) override
        {
            return RHI::ResultCode::Success;
        }
        RHI::ResultCode InitImageInternal(const RHI::ImageInitRequest&) override
        {
            return RHI::ResultCode::Success;
        }
    };

    class SceneFactory final : public Spark::Test::FakeHostBufferFactory
    {
    public:
        Ptr<RHI::ImagePool> m_scenePool;
        Ptr<RHI::ImagePool> CreateImagePool() override
        {
            m_scenePool = new SceneImagePool;
            return m_scenePool;
        }
        Ptr<RHI::Image> CreateImage() override { return Ptr<RHI::Image>(new SceneImage); }
    };

    class SceneAssets final : public Service<Resource::AssetManager>::Handler
    {
    public:
        eastl::vector<Ptr<Resource::ImageAsset>> m_images;
        uint32_t m_loads = 0;
        Ptr<Resource::Asset> FindAsset(const Resource::AssetId& id) const override
        {
            for (const auto& image : m_images)
            {
                if (image->GetAssetId() == id)
                {
                    return image;
                }
            }
            return {};
        }
        Ptr<Resource::Asset> LoadAsset(const Resource::AssetId& id) override { ++m_loads; return FindAsset(id); }
        Ptr<Resource::Asset> RequestAsset(const Resource::AssetId& id) override { return FindAsset(id); }
        Resource::AssetId MakeAssetId(eastl::string_view path) override
        {
            for (const auto& image : m_images)
            {
                if (image->GetPath() == path)
                {
                    return image->GetAssetId();
                }
            }
            return {};
        }
        eastl::vector<Resource::AssetId> GetRegisteredAssetIds() const override { return {}; }
        Resource::AssetType GetSupportAssetType(eastl::string_view) override { return Resource::AssetType::Image; }
        void AssetRegistry() override {}
        Resource::AssetId SaveAsset(const Resource::Asset&, eastl::string_view) override { return {}; }
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "TestSceneAssets"_hs; }
    private:
        void InitInternal() override {}
        void ShutdownInternal() override {}
        void ReleaseAsset(const Resource::AssetId&, const Resource::Asset*) override {}
    };
}

class SceneTextureSystemTest : public ::testing::Test
{
protected:
    WorldContext world;
    RHI::RHIContext rhi;
    RHI::RHIEventContext events;
    SceneFactory factory;
    SceneAssets assets;
    Ptr<RHI::PhysicalDevice> physical{ new RHI::PhysicalDevice };
    Ptr<Spark::Test::FakeDevice> device{ new Spark::Test::FakeDevice };
    Skybox::SkyboxSystem skyboxes;
    SceneTextureSystem images;

    void SetUp() override
    {
        WorldExecuteContext::Push(world);
        RHI::RHIExecuteContext::Push(rhi);
        RHI::RHIEventExecuteContext::Push(events);
        ASSERT_EQ(device->Init(*physical, RHI::DeviceDescriptor{}), RHI::ResultCode::Success);
        AddLut("engine://Image/BRDFLut.ktx2");
        AddLut("engine://Image/LTC1.ktx2");
        AddLut("engine://Image/LTC2.ktx2");
        skyboxes.Init();
        images.Init(rhi, *device);
    }

    void TearDown() override
    {
        skyboxes.Shutdown();
        // Simulate the uploader stopping before the renderer releases its pool.
        events.Clear();
        images.Shutdown();
        RHI::RHIHandleClearSystem::Collect(rhi);
        device->FlushReleaseQueue();
        device->BusDisconnect();
        RHI::RHIEventExecuteContext::Pop();
        RHI::RHIExecuteContext::Pop();
        WorldExecuteContext::Pop();
    }

    void AddLut(const char* path)
    {
        Resource::ImageAssetDescriptor descriptor;
        descriptor.compression = Resource::TextureCompression::None;
        descriptor.colorSpace = Resource::ImageColorSpace::Linear;
        descriptor.maxMipLevels = 1;
        const auto id = Resource::AssetId::Of<Resource::ImageAsset>(path, descriptor);
        Ptr<Resource::ImageAsset> asset = new Resource::ImageAsset(id);
        Resource::ImageAssetRawData raw(1, 1, Resource::ImageFormat::RGBA8, { 0, 0, 0, 255 }, path);
        Resource::ImageAssetCompiler compiler;
        eastl::vector<Resource::SubAssetEntry> children;
        asset->SetDataReady(compiler.Compile(id, raw, children));
        assets.m_images.push_back(asset);
    }

    static void MakeReady(Resource::ImageAsset& asset)
    {
        Resource::BakedCubemap cube;
        cube.faceSize = 1;
        cube.faceBytes.resize(6 * 8);
        Resource::ImageAssetCompiler compiler;
        asset.SetDataReady(compiler.AssembleCubemapData(eastl::move(cube)));
    }

    Ptr<Resource::ImageAsset> AddSky(const char* path, bool ready = true)
    {
        const auto id = Resource::AssetId::Of<Resource::ImageAsset>(path)
            .WithDescriptor(Resource::ImageAsset::DefaultHDRDescriptor());
        Ptr<Resource::ImageAsset> asset = new Resource::ImageAsset(id);
        if (ready)
        {
            MakeReady(*asset);
        }
        assets.m_images.push_back(asset);
        return asset;
    }

    Entity AddSkybox(const Ptr<Resource::ImageAsset>& asset)
    {
        const auto entity = world.CreateEntity();
        world.Add<Skybox::SkyboxComponent>(entity, Skybox::SkyboxComponent{ asset->GetAssetId(), 1.0f });
        return entity;
    }

    template<typename Tag>
    RHI::RHIHandle FindTable()
    {
        RHI::RHIHandle table = RHI::NullHandle;
        rhi.GetView<Tag>(Exclude<DeadTag>).each([&](RHI::RHIHandle entity) { table = entity; });
        return table;
    }
};

TEST_F(SceneTextureSystemTest, OnlyTheFirstCreatedSkyboxIsActiveAndEditsKeepTheSelection)
{
    const auto first = AddSkybox(AddSky("test://sky-a"));
    const auto second = AddSkybox(AddSky("test://sky-b"));
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(first));
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(second));
    EXPECT_EQ(world.GetView<SkyboxGPUComponent>().size(), 0u);
    EXPECT_EQ(rhi.GetView<RHI::Components::Image>().size(), 3u);

    auto component = world.Get<Skybox::SkyboxComponent>(first);
    component.m_intensity = 2.0f;
    world.Replace<Skybox::SkyboxComponent>(first, component);
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(first));
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(second));
    component.m_imageAssetId = AddSky("test://replacement")->GetAssetId();
    world.Replace<Skybox::SkyboxComponent>(first, component);
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(first));
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(second));
    world.Remove<Skybox::SkyboxComponent>(second);
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(first));
    world.Remove<Skybox::SkyboxComponent>(first);
    EXPECT_EQ(world.GetView<Skybox::ActiveSkyboxTag>().size(), 0u);
    const auto third = AddSkybox(AddSky("test://sky-c"));
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(third));
}

TEST_F(SceneTextureSystemTest, OnlyNewSkyboxesCanClaimSelectionAfterTheActiveEntityIsDestroyed)
{
    const auto first = AddSkybox(AddSky("test://sky-a"));
    const auto second = AddSkybox(AddSky("test://sky-b"));
    images.Update(rhi);
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(first));
    EXPECT_TRUE(rhi.Has<ActiveSkyCubeTag>(world.Get<SkyboxGPUComponent>(first).m_cubemap.Get()));

    world.DestoryEntity(first);
    images.Update(rhi);
    EXPECT_EQ(world.GetView<Skybox::ActiveSkyboxTag>().size(), 0u);
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(second));
    EXPECT_EQ(rhi.GetView<ActiveSkyCubeTag>().size(), 0u);

    auto component = world.Get<Skybox::SkyboxComponent>(second);
    component.m_intensity = 2.0f;
    world.Replace<Skybox::SkyboxComponent>(second, component);
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(second));

    const auto third = AddSkybox(AddSky("test://sky-c"));
    images.Update(rhi);
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(third));
    EXPECT_EQ(world.GetView<Skybox::ActiveSkyboxTag>().size(), 1u);
    EXPECT_TRUE(rhi.Has<ActiveSkyCubeTag>(world.Get<SkyboxGPUComponent>(third).m_cubemap.Get()));
}

TEST_F(SceneTextureSystemTest, BatchCreationAlsoSelectsOnlyTheFirstSkybox)
{
    const auto asset = AddSky("test://batch");
    const Entity batch[] = { world.CreateEntity(), world.CreateEntity() };
    world.Add<Skybox::SkyboxComponent>(batch, batch + 2,
        Skybox::SkyboxComponent{ asset->GetAssetId(), 1.0f });
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(batch[0]));
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(batch[1]));
    EXPECT_EQ(world.GetView<Skybox::ActiveSkyboxTag>().size(), 1u);
}

TEST_F(SceneTextureSystemTest, StaticImagesAreCreatedImmediatelyAndUploadedOnce)
{
    const auto sky = AddSkybox(AddSky("test://sky"));
    images.Update(rhi);
    const auto cube = world.Get<SkyboxGPUComponent>(sky).m_cubemap.Get();
    EXPECT_EQ(rhi.GetView<RHI::PendingImageInit>().size(), 0u);
    EXPECT_EQ(rhi.GetView<RHI::PerFrameTag>().size(), 0u);
    EXPECT_TRUE(rhi.Has<ActiveSkyCubeTag>(cube));
    EXPECT_TRUE(rhi.Has<RHI::Components::Image>(cube));
    EXPECT_FALSE(RHI::IsResourceReady(rhi, cube));
    EXPECT_EQ(factory.m_scenePool->GetDescriptor().m_heapMemoryLevel, RHI::HeapMemoryLevel::Device);
    const auto brdf = FindTable<BRDFLutTag>();
    const auto ltc1 = FindTable<LTC1Tag>();
    const auto ltc2 = FindTable<LTC2Tag>();
    ASSERT_NE(brdf, RHI::NullHandle);
    ASSERT_NE(ltc1, RHI::NullHandle);
    ASSERT_NE(ltc2, RHI::NullHandle);
    EXPECT_EQ(rhi.GetView<SceneLookupTableTag>().size(), 3u);
    EXPECT_STREQ(rhi.Get<RHI::ResourceName>(brdf).m_name.GetCStr(), "BRDFLut");
    EXPECT_STREQ(rhi.Get<RHI::ResourceName>(ltc1).m_name.GetCStr(), "LTC1");
    EXPECT_STREQ(rhi.Get<RHI::ResourceName>(ltc2).m_name.GetCStr(), "LTC2");
    EXPECT_EQ(rhi.Get<RHI::Components::Image>(brdf).m_image->GetDescriptor().m_arraySize, 1u);
    EXPECT_EQ(rhi.Get<RHI::Components::Image>(cube).m_image->GetDescriptor().m_arraySize, 6u);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 4u);

    auto component = world.Get<Skybox::SkyboxComponent>(sky);
    component.m_intensity = 3.0f;
    world.Replace<Skybox::SkyboxComponent>(sky, component);
    images.Update(rhi);
    EXPECT_EQ(world.Get<SkyboxGPUComponent>(sky).m_cubemap.Get(), cube);
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 4u);
    EXPECT_EQ(assets.m_loads, 3u); // Only the engine lookup tables load synchronously.
}

TEST_F(SceneTextureSystemTest, AnAssetBecomingReadyNeedsNoComponentRewrite)
{
    const auto asset = AddSky("test://late", false);
    const auto sky = AddSkybox(asset);
    images.Update(rhi);
    EXPECT_FALSE(world.Has<SkyboxGPUComponent>(sky));
    MakeReady(*asset);
    images.Update(rhi);
    EXPECT_TRUE(world.Has<SkyboxGPUComponent>(sky));
}

TEST_F(SceneTextureSystemTest, VisibleSkyAndLightingUseTheSameActiveWorldEntity)
{
    const auto asset = AddSky("test://environment");
    for (const auto& id : { asset->IrradianceId(), asset->PrefilteredId() })
    {
        Ptr<Resource::ImageAsset> child = new Resource::ImageAsset(id);
        MakeReady(*child);
        assets.m_images.push_back(child);
    }
    const auto sky = AddSkybox(asset);
    images.Update(rhi);
    const auto& gpu = world.Get<SkyboxGPUComponent>(sky);
    EXPECT_TRUE(rhi.Has<ActiveSkyCubeTag>(gpu.m_cubemap.Get()));
    EXPECT_TRUE(rhi.Has<SkyLightingTag>(gpu.m_irradiance.Get()));
    EXPECT_TRUE(rhi.Has<SkyLightingTag>(gpu.m_prefiltered.Get()));
    EXPECT_EQ(events.GetView<RHI::ImageUploadRequest>().size(), 6u);

    const auto other = AddSkybox(AddSky("test://without-ibl"));
    EXPECT_TRUE(world.Has<Skybox::ActiveSkyboxTag>(sky));
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(other));
    const auto oldCube = gpu.m_cubemap.Get();
    world.DestoryEntity(sky);
    const auto replacement = AddSkybox(AddSky("test://new-without-ibl"));
    images.Update(rhi);
    EXPECT_FALSE(rhi.Has<ActiveSkyCubeTag>(oldCube));
    EXPECT_FALSE(world.Has<Skybox::ActiveSkyboxTag>(other));
    EXPECT_TRUE(rhi.Has<ActiveSkyCubeTag>(world.Get<SkyboxGPUComponent>(replacement).m_cubemap.Get()));
    EXPECT_EQ(rhi.GetView<SkyLightingTag>().size(), 0u);
}

TEST_F(SceneTextureSystemTest, ReplacingAndRemovingSkyboxesRetiresTheirImages)
{
    const auto sky = AddSkybox(AddSky("test://old"));
    images.Update(rhi);
    const auto oldImage = world.Get<SkyboxGPUComponent>(sky).m_cubemap.Get();
    const auto replacement = AddSky("test://new");
    world.Replace<Skybox::SkyboxComponent>(sky, Skybox::SkyboxComponent{ replacement->GetAssetId(), 1.0f });
    images.Update(rhi);
    const auto newImage = world.Get<SkyboxGPUComponent>(sky).m_cubemap.Get();
    EXPECT_NE(newImage, oldImage);
    EXPECT_TRUE(rhi.Has<DeadTag>(oldImage));
    EXPECT_FALSE(rhi.Has<ActiveSkyCubeTag>(oldImage));
    EXPECT_TRUE(rhi.Has<ActiveSkyCubeTag>(newImage));
    world.Remove<Skybox::SkyboxComponent>(sky);
    images.Update(rhi);
    EXPECT_FALSE(world.Has<SkyboxGPUComponent>(sky));
    EXPECT_TRUE(rhi.Has<DeadTag>(newImage));
    EXPECT_EQ(rhi.GetView<ActiveSkyCubeTag>().size(), 0u);
}

TEST_F(SceneTextureSystemTest, UploadRequestKeepsSourceAfterTheWorldEntityIsGone)
{
    auto asset = AddSky("test://retired");
    const auto sky = AddSkybox(asset);
    images.Update(rhi);
    const auto image = world.Get<SkyboxGPUComponent>(sky).m_cubemap.Get();
    RHI::RHIEvent upload = RHI::NullEvent;
    for (auto [request, data] : events.GetView<RHI::ImageUploadRequest>().each())
    {
        if (data.m_target == image)
        {
            upload = request;
        }
    }
    ASSERT_NE(upload, RHI::NullEvent);
    world.DestoryEntity(sky);
    rhi.DestoryEntity(image);
    assets.m_images.pop_back();
    EXPECT_EQ(asset->UseCount(), 2u); // This test and the in-flight upload.
    EXPECT_EQ(events.Get<RHI::ImageUploadRequest>(upload).m_data, asset->GetImageData()->GetTextureBytes().data());
    RHI::FinishUploadRequest(rhi, events, upload);
    EXPECT_EQ(asset->UseCount(), 1u);
}

TEST_F(SceneTextureSystemTest, ShutdownMarksOwnedImagesForTheCommonReaper)
{
    const auto sky = AddSkybox(AddSky("test://shutdown"));
    images.Update(rhi);
    const auto cube = world.Get<SkyboxGPUComponent>(sky).m_cubemap.Get();
    const auto brdf = FindTable<BRDFLutTag>();
    const auto ltc1 = FindTable<LTC1Tag>();
    const auto ltc2 = FindTable<LTC2Tag>();
    events.Clear();
    images.Shutdown();
    EXPECT_EQ(world.GetView<SkyboxGPUComponent>().size(), 0u);
    for (auto image : { cube, brdf, ltc1, ltc2 })
    {
        EXPECT_TRUE(rhi.Valid(image));
        EXPECT_TRUE(rhi.Has<DeadTag>(image));
    }
    EXPECT_TRUE(factory.m_scenePool->IsInitialized());
    EXPECT_EQ(FindTable<BRDFLutTag>(), RHI::NullHandle);
    EXPECT_EQ(FindTable<LTC1Tag>(), RHI::NullHandle);
    EXPECT_EQ(FindTable<LTC2Tag>(), RHI::NullHandle);
    RHI::RHIHandleClearSystem::Collect(rhi);
    EXPECT_EQ(rhi.GetView<RHI::Components::Image>().size(), 0u);
}
