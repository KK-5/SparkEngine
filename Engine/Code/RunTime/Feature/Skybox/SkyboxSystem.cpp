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
    namespace
    {
        //! Create the GPU image for one compiled image asset and queue its upload.
        //! `staticImport` picks StaticImportTag (sampled directly, no attachment) over
        //! ImportedTag (a pass must register it). Returns NullHandle on failure.
        RHI::RHIHandle CreateAndUploadImage(RHI::RHIContext& rhiCtx,
                                            const Resource::ImageAsset& asset,
                                            const eastl::string& name,
                                            bool staticImport)
        {
            const Resource::ImageAssetData* data = asset.GetImageData();
            if (!data || data->GetTextureBytes().empty())
            {
                LOG_ERROR("[SkyboxSystem] Image asset '{}' has no data.", name.c_str());
                return RHI::NullHandle;
            }

            const bool isCube = data->GetArrayLayers() == RHI::ImageDescriptor::NumCubeMapSlices;

            RHI::ImageDescriptor desc = isCube
                ? RHI::ImageDescriptor::CreateCubemap(
                      RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
                      asset.GetWidth(), asset.GetFormat())
                : RHI::ImageDescriptor::Create2D(
                      RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
                      asset.GetWidth(), asset.GetHeight(), asset.GetFormat());
            desc.m_mipLevels       = static_cast<uint16_t>(asset.GetMipLevels());
            desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

            // StaticImportTag vs ImportedTag decides who emits the CopyDst->ShaderRead
            // barrier: static imports are handled once by the compiler on first access,
            // imported ones need a pass to declare them as an attachment -- which is why
            // the sky cube, and only the sky cube, is imported (SkyboxPass registers it).
            RHI::RHIHandle imageEntity = staticImport
                ? RHI::CreateStaticImage(
                      rhiCtx, ObjectName(name), desc,
                      RHI::HeapMemoryLevel::Device, RHI::HostMemoryAccess::Write)
                : RHI::CreateImportedImage(
                      rhiCtx, ObjectName(name), desc,
                      RHI::HeapMemoryLevel::Device, RHI::HostMemoryAccess::Write);

            RHI::RequestImageUpload(
                rhiCtx, imageEntity,
                data->GetTextureBytes().data(),
                data->GetTextureBytes().size(),
                RHI::ImageSubresourceRange(desc),
                RHI::Origin(),
                asset.GetFormat());

            return imageEntity;
        }
    }

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
        // The GPU resources depend on the asset id ALONE. Every other field (intensity, and
        // whatever else lands on SkyboxComponent later) is read per-frame straight off the
        // component by SceneBindingSystem, so re-uploading three cubemaps because a slider
        // moved would be pure waste -- the sky cube alone is tens of megabytes.
        //
        // Still rebuilds when the resources are missing even though the id matches: that is
        // the "asset finished loading, upper layer re-fires the update" path.
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* comp    = ctx.TryGet<SkyboxComponent>(entity);
        auto* gpuComp = ctx.TryGet<SkyboxGPUComponent>(entity);
        if (comp && gpuComp && gpuComp->m_cubemapAsset
            && gpuComp->m_cubemapAsset->GetAssetId() == comp->m_imageAssetId)
        {
            return;
        }

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

        const eastl::string idSuffix = eastl::to_string(static_cast<uint32_t>(entity));

        RHI::RHIHandle cubeEntity = CreateAndUploadImage(
            *rhiCtx, *asset, eastl::string("SkyboxCube_") + idSuffix, /*staticImport*/ false);
        if (cubeEntity == RHI::NullHandle)
        {
            return;
        }

        // The IBL products are optional: a cubemap asset that predates the environment bake
        // (or any non-environment cube assigned here) simply has none, and the lighting path
        // falls back to its constant ambient. Their readiness needs no check -- the asset
        // layer publishes them before the parent goes Ready.
        SkyboxGPUComponent gpuComp;
        if (const Ptr<Resource::ImageAsset>& irradiance = asset->GetIrradianceAsset())
        {
            gpuComp.m_irradiance = CreateAndUploadImage(
                *rhiCtx, *irradiance, eastl::string("SkyboxIrradiance_") + idSuffix,
                /*staticImport*/ true);
        }
        if (const Ptr<Resource::ImageAsset>& prefiltered = asset->GetPrefilteredAsset())
        {
            gpuComp.m_prefiltered = CreateAndUploadImage(
                *rhiCtx, *prefiltered, eastl::string("SkyboxPrefiltered_") + idSuffix,
                /*staticImport*/ true);
        }

        // Designate this as the active skybox's cube so the render SkyboxPass imports it.
        // Tagged at creation (materialization is not required to tag — SkyboxPass gates the
        // import on the backing image itself). Singleton: clear any prior cube's tag first.
        // SceneBindingSystem also uses this tag to pick which skybox's IBL images to bind.
        rhiCtx->Clear<ActiveSkyCubeTag>();
        rhiCtx->Add<ActiveSkyCubeTag>(cubeEntity);

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

        if (rhiCtx)
        {
            for (RHI::RHIHandle handle : {gpuComp->m_cubemap, gpuComp->m_irradiance,
                                          gpuComp->m_prefiltered})
            {
                if (rhiCtx->Valid(handle))
                {
                    rhiCtx->Add<DeadTag>(handle);
                }
            }
        }

        ctx.Remove<SkyboxGPUComponent>(entity);
    }
}
