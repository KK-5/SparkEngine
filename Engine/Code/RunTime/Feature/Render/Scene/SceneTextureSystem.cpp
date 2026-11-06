#include "SceneTextureSystem.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <CoreComponents/Tags.h>
#include <ECS/Common.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <Resource/AssetManagerInterface.h>
#include <Skybox/Components.h>

namespace Spark::Render
{
    namespace
    {
        //! The upload thread reads these bytes after the source skybox may have been removed.
        //! Kept on the request, through its copy and acquire, independently of the world entity.
        struct SceneImageUploadSource
        {
            Ptr<Resource::ImageAsset> m_asset;
        };
    }

    SceneTextureSystem::SceneTextureSystem() = default;

    SceneTextureSystem::~SceneTextureSystem()
    {
        if (m_pool)
        {
            m_pool->Shutdown();
        }
    }

    void SceneTextureSystem::Init(RHI::RHIContext& rhiCtx, RHI::Device& device)
    {
        RHI::ImagePoolDescriptor descriptor;
        descriptor.m_bindFlags = RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite;
        m_pool = Service<RHI::Factory>::Get()->CreateImagePool();
        m_pool->SetName(ObjectName("SceneTextures"));
        if (m_pool->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[SceneTextureSystem] Image pool init failed.");
            m_pool.reset();
            return;
        }

        m_brdfLut = CreateLut<BRDFLutTag>(rhiCtx, "engine://Image/BRDFLut.ktx2", "BRDFLut");
        m_ltc1 = CreateLut<LTC1Tag>(rhiCtx, "engine://Image/LTC1.ktx2", "LTC1");
        m_ltc2 = CreateLut<LTC2Tag>(rhiCtx, "engine://Image/LTC2.ktx2", "LTC2");
    }

    RHI::RHIHandle SceneTextureSystem::CreateImage(
        RHI::RHIContext& rhiCtx, const Ptr<Resource::ImageAsset>& asset, ObjectName name)
    {
        const auto* data = asset ? asset->GetImageData() : nullptr;
        if (!asset || !asset->IsReady() || !data || data->GetTextureBytes().empty())
        {
            LOG_ERROR("[SceneTextureSystem] Image '{}' is not ready or has no data.", name.GetCStr());
            return RHI::NullHandle;
        }
        constexpr auto flags = RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite;
        RHI::ImageDescriptor descriptor = data->IsCubemap()
            ? RHI::ImageDescriptor::CreateCubemap(flags, asset->GetWidth(), asset->GetFormat())
            : RHI::ImageDescriptor::Create2D(flags, asset->GetWidth(), asset->GetHeight(), asset->GetFormat());
        descriptor.m_mipLevels = static_cast<uint16_t>(asset->GetMipLevels());
        descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        Ptr<RHI::Image> image = Service<RHI::Factory>::Get()->CreateImage();
        image->SetName(name);
        RHI::ImageInitRequest init;
        init.m_image = image.get();
        init.m_descriptor = descriptor;
        if (m_pool->InitImage(init) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[SceneTextureSystem] Image '{}' failed to initialize.", name.GetCStr());
            return RHI::NullHandle;
        }

        const auto handle = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::ResourceName>(handle, RHI::ResourceName{ name });
        rhiCtx.Add<RHI::Components::Image>(handle, RHI::Components::Image{ eastl::move(image) });
        const auto request = RHI::RequestImageUpload(rhiCtx, handle,
            data->GetTextureBytes().data(), data->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(descriptor), RHI::Origin(), asset->GetFormat());
        RHI::RHIEventExecuteContext::Current()->Add<SceneImageUploadSource>(request, SceneImageUploadSource{ asset });
        return handle;
    }

    template<typename Tag>
    RHI::UniqueRHIHandle SceneTextureSystem::CreateLut(RHI::RHIContext& rhiCtx, const char* path, const char* name)
    {
        auto* manager = Service<Resource::AssetManager>::Get();
        const auto id = manager ? manager->MakeAssetId(path) : Resource::AssetId{};
        if (!id.IsValid())
        {
            LOG_ERROR("[SceneTextureSystem] Lookup table '{}' could not be resolved.", path);
            return {};
        }
        const auto handle = CreateImage(rhiCtx, manager->LoadAsset<Resource::ImageAsset>(id), ObjectName(name));
        if (handle != RHI::NullHandle)
        {
            rhiCtx.Add<SceneLookupTableTag>(handle);
            rhiCtx.Add<Tag>(handle);
        }
        return RHI::UniqueRHIHandle(handle);
    }

    void SceneTextureSystem::CreateSkybox(WorldContext& world, RHI::RHIContext& rhiCtx, Entity entity,
        const Ptr<Resource::ImageAsset>& asset)
    {
        const eastl::string suffix = eastl::to_string(static_cast<uint32_t>(entity));
        SkyboxGPUComponent gpu;
        gpu.m_cubemap = RHI::UniqueRHIHandle(CreateImage(rhiCtx, asset, ObjectName(eastl::string("SkyboxCube_") + suffix)));
        if (!gpu.m_cubemap.IsValid())
        {
            return;
        }
        if (const auto irradiance = asset->GetIrradianceAsset())
        {
            gpu.m_irradiance = RHI::UniqueRHIHandle(CreateImage(rhiCtx, irradiance,
                ObjectName(eastl::string("SkyboxIrradiance_") + suffix)));
        }
        if (const auto prefiltered = asset->GetPrefilteredAsset())
        {
            gpu.m_prefiltered = RHI::UniqueRHIHandle(CreateImage(rhiCtx, prefiltered,
                ObjectName(eastl::string("SkyboxPrefiltered_") + suffix)));
        }
        gpu.m_cubemapAsset = asset;
        world.Add<SkyboxGPUComponent>(entity, eastl::move(gpu));
    }

    void SceneTextureSystem::Update(RHI::RHIContext& rhiCtx)
    {
        auto* world = WorldExecuteContext::Current();
        if (!world || !m_pool)
        {
            return;
        }
        rhiCtx.Clear<ActiveSkyCubeTag>();
        rhiCtx.Clear<SkyLightingTag>();

        eastl::vector<Entity> stale;
        for (auto [entity, gpu] : world->GetView<SkyboxGPUComponent>().each())
        {
            const auto* sky = world->TryGet<Skybox::SkyboxComponent>(entity);
            if (!sky || world->Has<DeadTag>(entity) || !gpu.m_cubemapAsset
                || gpu.m_cubemapAsset->GetAssetId() != sky->m_imageAssetId)
            {
                stale.push_back(entity);
            }
        }
        for (Entity entity : stale)
        {
            world->Remove<SkyboxGPUComponent>(entity);
        }

        auto* manager = Service<Resource::AssetManager>::Get();
        for (auto [entity, sky] : world->GetView<Skybox::SkyboxComponent>(Exclude<DeadTag>).each())
        {
            if (!world->Has<SkyboxGPUComponent>(entity) && sky.m_imageAssetId.IsValid() && manager)
            {
                const auto asset = manager->FindAsset(sky.m_imageAssetId);
                if (asset && asset->GetAssetType() == Resource::AssetType::Image && asset->IsReady())
                {
                    CreateSkybox(*world, rhiCtx, entity, Ptr<Resource::ImageAsset>(static_cast<Resource::ImageAsset*>(asset.get())));
                }
            }
            const auto* gpu = world->TryGet<SkyboxGPUComponent>(entity);
            if (world->Has<Skybox::ActiveSkyboxTag>(entity) && gpu)
            {
                rhiCtx.Add<ActiveSkyCubeTag>(gpu->m_cubemap.Get());
                for (auto lighting : { gpu->m_irradiance.Get(), gpu->m_prefiltered.Get() })
                {
                    if (lighting != RHI::NullHandle)
                    {
                        rhiCtx.Add<SkyLightingTag>(lighting);
                    }
                }
            }
        }
    }

    void SceneTextureSystem::Shutdown()
    {
        m_brdfLut.Reset();
        m_ltc1.Reset();
        m_ltc2.Reset();
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<SkyboxGPUComponent>();
        }
    }
}
