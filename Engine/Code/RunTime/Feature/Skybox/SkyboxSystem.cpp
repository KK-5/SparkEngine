#include "SkyboxSystem.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

#include <EASTL/string.h>

namespace Spark::Skybox
{
    void SkyboxSystem::InitInternal()
    {
        ComponentEventBus::Handler::BusConnect(GetTypeId<SkyboxComponent>());

        auto& world = *WorldExecuteContext::Current();
        world.RegisterEventOnEntityRemove<SkyboxComponent>();
    }

    void SkyboxSystem::ShutdownInternal()
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        ctx.GetView<SkyboxGPUComponent>().each([&](Entity entity, SkyboxGPUComponent&)
        {
            CleanupGPUResources(entity);
        });

        ComponentEventBus::Handler::BusDisconnect();
    }

    void SkyboxSystem::OnComponentConstruct(Entity entity)
    {
        Resolve(entity);
    }

    void SkyboxSystem::OnComponentUpdated(Entity entity)
    {
        // Asset re-assigned (or params changed): drop the old GPU texture and
        // re-resolve from the current asset.
        CleanupGPUResources(entity);
        Resolve(entity);
    }

    void SkyboxSystem::OnComponentDestory(Entity entity)
    {
        CleanupGPUResources(entity);
    }

    void SkyboxSystem::Resolve(Entity entity)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();

        auto* comp = ctx.TryGet<SkyboxComponent>(entity);
        if (!comp || !comp->m_imageAssetId.IsValid())
        {
            return;
        }

        // Loading is the upper layer's job (the editor assigns an already-loaded
        // asset). The system only consumes it: look up the loaded asset and, if
        // ready, organize it into GPU resources. Not ready → skip; the upper layer
        // re-fires OnComponentUpdated once it finishes loading. Never LoadAsset here.
        auto* assetManager = Service<Resource::AssetManager>::Get();
        if (!assetManager)
        {
            LOG_ERROR("[SkyboxSystem] AssetManager service missing.");
            return;
        }

        Ptr<Resource::Asset> found = assetManager->FindAsset(comp->m_imageAssetId);
        if (!found || found->GetAssetType() != Resource::AssetType::Image)
        {
            LOG_WARN("[SkyboxSystem] Skybox image asset not found / wrong type: {}",
                     comp->m_imageAssetId.GetPath().c_str());
            return;
        }

        Ptr<Resource::ImageAsset> image(static_cast<Resource::ImageAsset*>(found.get()));
        if (image->GetStatus() != Resource::AssetStatus::Ready)
        {
            LOG_WARN("[SkyboxSystem] Skybox image not ready yet (loading is the upper "
                     "layer's job): {}", comp->m_imageAssetId.GetPath().c_str());
            return;
        }

        BuildGPUResources(entity, image);
    }

    void SkyboxSystem::BuildGPUResources(Entity entity, const Ptr<Resource::ImageAsset>& asset)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            LOG_ERROR("[SkyboxSystem] RHI context not available, cannot create GPU resources.");
            return;
        }

        const Resource::ImageAssetData* data = asset->GetImageData();
        if (!data || data->GetTextureBytes().empty())
        {
            LOG_ERROR("[SkyboxSystem] Skybox image asset has no data.");
            return;
        }

        const bool isCube = data->GetArrayLayers() == RHI::ImageDescriptor::NumCubeMapSlices;

        RHI::ImageDescriptor desc = isCube
            ? RHI::ImageDescriptor::CreateCubemap(
                  RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
                  asset->GetWidth(), asset->GetFormat())
            : RHI::ImageDescriptor::Create2D(
                  RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
                  asset->GetWidth(), asset->GetHeight(), asset->GetFormat());
        desc.m_mipLevels       = static_cast<uint16_t>(asset->GetMipLevels());
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        const eastl::string idSuffix = eastl::to_string(static_cast<uint32_t>(entity));
        const eastl::string cubeName = eastl::string("SkyboxCube_") + idSuffix;

        RHI::RHIHandle cubeEntity = RHI::CreateImportedImage(
            *rhiCtx, ObjectName(cubeName), desc,
            RHI::HeapMemoryLevel::Device, RHI::HostMemoryAccess::Write);

        RHI::RequestImageUpload(
            *rhiCtx, cubeEntity,
            data->GetTextureBytes().data(),
            data->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(desc),
            RHI::Origin(),
            asset->GetFormat());

        // Designate this as the active skybox's cube so the render SkyboxPass imports it.
        // Tagged at creation (materialization is not required to tag — SkyboxPass gates the
        // import on the backing image itself). Singleton: clear any prior cube's tag first.
        rhiCtx->Clear<ActiveSkyCubeTag>();
        rhiCtx->Add<ActiveSkyCubeTag>(cubeEntity);

        SkyboxGPUComponent gpuComp;
        gpuComp.m_cubemapAsset = asset;
        gpuComp.m_cubemap      = cubeEntity;
        ctx.AddOrReplace<SkyboxGPUComponent>(entity, eastl::move(gpuComp));
    }

    void SkyboxSystem::CleanupGPUResources(Entity entity)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();

        auto* gpuComp = ctx.TryGet<SkyboxGPUComponent>(entity);
        if (!gpuComp)
        {
            return;
        }

        if (rhiCtx && rhiCtx->Valid(gpuComp->m_cubemap))
        {
            rhiCtx->Add<DeadTag>(gpuComp->m_cubemap);
        }

        ctx.Remove<SkyboxGPUComponent>(entity);
    }
}
