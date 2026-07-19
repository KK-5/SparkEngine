#include "MaterialTextureSystem.h"

#include <EASTL/string.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <Object/ObjectName.h>
#include <CoreComponents/Tags.h>

#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>

#include <Pass/Component/RHIComponents.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

#include <Material/Components.h>
#include <Material/MaterialContext.h>

#include "MaterialBinding.h"

namespace Spark::Render
{
    void MaterialTextureSystem::Init(RHI::RHIContext&)
    {
    }

    void MaterialTextureSystem::Update()
    {
        auto* matCtx = Material::MaterialExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!matCtx || !rhiCtx)
        {
            return;
        }
        auto* assetManager = Service<Resource::AssetManager>::Get();
        if (!assetManager)
        {
            return;
        }

        matCtx->GetView<Material::MaterialParams>().each(
            [&](Material::MaterialHandle h, Material::MaterialParams& params)
        {
            RHI::RHIHandle tex = RHI::NullHandle;

            if (params.m_baseColorTexture.IsValid())
            {
                // Loading is the editor's job (mirrors SkyboxSystem): FindAsset the
                // already-loaded asset, never LoadAsset here.
                if (!params.m_baseColorImage)
                {
                    Ptr<Resource::Asset> found = assetManager->FindAsset(params.m_baseColorTexture);
                    if (found && found->GetAssetType() == Resource::AssetType::Image)
                    {
                        params.m_baseColorImage = Ptr<Resource::ImageAsset>(
                            static_cast<Resource::ImageAsset*>(found.get()));
                    }
                }

                if (params.m_baseColorImage &&
                    params.m_baseColorImage->GetStatus() == Resource::AssetStatus::Ready)
                {
                    tex = EnsureResident(*rhiCtx, params.m_baseColorTexture, params.m_baseColorImage);
                }
            }

            matCtx->AddOrReplace<MaterialGPUTextures>(h, MaterialGPUTextures{ tex });
        });
    }

    void MaterialTextureSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        for (auto& entry : m_pool)
        {
            if (entry.second != RHI::NullHandle)
            {
                rhiCtx.Add<DeadTag>(entry.second);
            }
        }
        m_pool.clear();
    }

    RHI::RHIHandle MaterialTextureSystem::EnsureResident(
        RHI::RHIContext& rhiCtx, const Resource::AssetId& id, const Ptr<Resource::ImageAsset>& img)
    {
        if (auto it = m_pool.find(id); it != m_pool.end())
        {
            return it->second;
        }

        const Resource::ImageAssetData* data = img->GetImageData();
        if (!data || data->GetTextureBytes().empty())
        {
            return RHI::NullHandle;
        }

        RHI::ImageDescriptor desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
            img->GetWidth(), img->GetHeight(), img->GetFormat());
        desc.m_mipLevels       = static_cast<uint16_t>(img->GetMipLevels());
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        const eastl::string name = eastl::string("MaterialTex_") +
            eastl::to_string(static_cast<unsigned long long>(id.GetHash()));

        RHI::RHIHandle tex = RHI::CreateStaticImage(
            rhiCtx, ObjectName(name), desc,
            RHI::HeapMemoryLevel::Device, RHI::HostMemoryAccess::Write);

        RHI::RequestImageUpload(
            rhiCtx, tex, data->GetTextureBytes().data(), data->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(desc), RHI::Origin(), img->GetFormat());

        Render::CreateStaticImageAttachment(
            rhiCtx, tex, RHI::InputName(name),
            RHI::AttachmentAccess::Read, RHI::AttachmentUsage::Shader, RHI::AttachmentStage::FragmentShader);

        m_pool.emplace(id, tex);
        return tex;
    }
}
