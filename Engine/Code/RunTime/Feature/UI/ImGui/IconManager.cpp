#include "IconManager.h"

#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

namespace Spark::UI
{
    Resource::AssetId IconManager::OpenIcon(eastl::string_view imagePath)
    {
        auto world = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        auto* assetMgr = Service<Resource::AssetManager>::Get();

        if (!rhiCtx || !assetMgr)
        {
            LOG_ERROR("[IconManager] Required services not available.");
            return {};
        }

        auto assetId = Resource::AssetId::Of<Resource::ImageAsset>(imagePath);
        Ptr<Resource::ImageAsset> imageAsset =
            assetMgr->LoadAsset<Resource::ImageAsset>(assetId);

        if (!imageAsset || !imageAsset->IsReady())
        {
            LOG_ERROR("[IconManager] Failed to load icon: {}", imagePath);
            return {};
        }

        const Resource::ImageAssetData* imageData = imageAsset->GetImageData();
        if (!imageData)
        {
            LOG_ERROR("[IconManager] Image data is null for: {}", imagePath);
            return {};
        }

        // World entity
        Entity worldEntity = world.CreateEntity();

        IconComponent iconComp;
        iconComp.m_iconAssetId = assetId;
        iconComp.m_iconImageAsset = eastl::move(imageAsset);
        world.Add<IconComponent>(worldEntity, eastl::move(iconComp));

        // Static GPU image
        RHI::ImageDescriptor imageDesc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead |  RHI::ImageBindFlags::CopyWrite,
            imageData->GetWidth(),
            imageData->GetHeight(),
            imageData->GetFormat());
        imageDesc.m_mipLevels = static_cast<uint16_t>(imageData->GetMipLevels());

        RHI::RHIHandle imageEntity = RHI::CreateStaticImage(
            *rhiCtx,
            ObjectName(imagePath),
            imageDesc);

        RHI::RequestImageUpload(
            *rhiCtx,
            imageEntity,
            imageData->GetTextureBytes().data(),
            imageData->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(
                0,
                static_cast<uint16_t>(imageData->GetMipLevels() - 1),
                0,
                0),
            {},
            imageData->GetFormat());

        // SRV view
        RHI::ImageViewDescriptor viewDesc;
        viewDesc.m_mipSliceMax = static_cast<uint16_t>(imageData->GetMipLevels() - 1);

        RHI::RHIHandle viewEntity = RHI::CreateImageView(
            *rhiCtx,
            imageEntity,
            ObjectName(eastl::string(imagePath) + "_view"),
            viewDesc);

        // GPU component on world entity
        IconGPUComponent gpuComp;
        gpuComp.m_image = imageEntity;
        gpuComp.m_imageView = viewEntity;
        world.AddOrReplace<IconGPUComponent>(worldEntity, eastl::move(gpuComp));

        return assetId;
    }

    ImTextureID IconManager::RequestIconId(Resource::AssetId id)
    {
        auto world = WorldExecuteContext::CurrentReference<SystemTraits>();
        const auto hash = id.GetHash();

        ImTextureID result = ImTextureID_Invalid;

        auto view = world.GetView<IconGPUComponent, IconComponent>();
        view.each([&](Entity, const IconGPUComponent& gpu, const IconComponent& icon)
        {
            if (icon.m_iconAssetId.GetHash() == hash)
            {
                result = gpu.m_iconId;
            }
        });

        return result;
    }

    void IconManager::ShutdownInternal()
    {
        auto world = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();

        auto view = world.GetView<IconGPUComponent>();
        eastl::vector<Entity> entities;
        view.each([&](Entity entity, const IconGPUComponent&)
        {
            entities.push_back(entity);
        });

        for (Entity entity : entities)
        {
            auto* gpuComp = world.TryGet<IconGPUComponent>(entity);
            if (!gpuComp)
            {
                continue;
            }

            if (rhiCtx)
            {
                if (gpuComp->m_image != RHI::NullHandle && rhiCtx->Valid(gpuComp->m_image))
                {
                    rhiCtx->DestoryEntity(gpuComp->m_image);
                }
                if (gpuComp->m_imageView != RHI::NullHandle && rhiCtx->Valid(gpuComp->m_imageView))
                {
                    rhiCtx->DestoryEntity(gpuComp->m_imageView);
                }
            }

            world.DestoryEntity(entity);
        }
    }
}