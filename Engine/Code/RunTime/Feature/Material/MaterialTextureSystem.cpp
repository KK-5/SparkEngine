#include "MaterialTextureSystem.h"

#include <EASTL/string.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <Object/ObjectName.h>
#include <CoreComponents/Tags.h>

#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

#include "Components.h"
#include "MaterialContext.h"

namespace Spark::Material
{
    void MaterialTextureSystem::Init()
    {
    }

    void MaterialTextureSystem::Update()
    {
        auto* matCtx = MaterialExecuteContext::Current();
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

        matCtx->GetView<Resource::StandardPBR>().each(
            [&](MaterialHandle h, const Resource::StandardPBR& params)
        {
            MaterialGPUTextures gpu;   // all slots default to NullHandle

            for (size_t slot = 0; slot < Resource::MaterialTexSlotCount; ++slot)
            {
                const Resource::AssetId& id = params.m_textures[slot];
                if (!id.IsValid())
                {
                    continue;
                }

                // The pool IS the resolve cache: a resident texture answers from here
                // without touching the asset database. Only a slot that is not resident
                // yet pays for a lookup, and only until its asset reaches Ready.
                if (auto it = m_pool.find(id); it != m_pool.end())
                {
                    gpu.m_handles[slot] = it->second.m_handle;
                    continue;
                }

                Ptr<Resource::Asset> found = assetManager->FindAsset(id);
                if (!found
                    || found->GetAssetType() != Resource::AssetType::Image
                    || found->GetStatus() != Resource::AssetStatus::Ready)
                {
                    continue;
                }

                gpu.m_handles[slot] = EnsureResident(*rhiCtx, id,
                    Ptr<Resource::ImageAsset>(static_cast<Resource::ImageAsset*>(found.get())));
            }

            matCtx->AddOrReplace<MaterialGPUTextures>(h, gpu);
        });
    }

    void MaterialTextureSystem::CollectGarbage()
    {
        auto* matCtx = MaterialExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!matCtx || !rhiCtx)
        {
            return;
        }

        ++m_gcGeneration;

        matCtx->GetView<Resource::StandardPBR>().each(
            [&](MaterialHandle, const Resource::StandardPBR& params)
        {
            for (const Resource::AssetId& id : params.m_textures)
            {
                if (id.IsValid())
                {
                    if (auto it = m_pool.find(id); it != m_pool.end())
                    {
                        it->second.m_gen = m_gcGeneration;
                    }
                }
            }
        });

        for (auto it = m_pool.begin(); it != m_pool.end();)
        {
            if (it->second.m_gen != m_gcGeneration)
            {
                if (it->second.m_handle != RHI::NullHandle)
                {
                    rhiCtx->Add<DeadTag>(it->second.m_handle);
                }
                it = m_pool.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void MaterialTextureSystem::Shutdown()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }
        for (auto& entry : m_pool)
        {
            if (entry.second.m_handle != RHI::NullHandle)
            {
                rhiCtx->Add<DeadTag>(entry.second.m_handle);
            }
        }
        m_pool.clear();
    }

    RHI::RHIHandle MaterialTextureSystem::EnsureResident(
        RHI::RHIContext& rhiCtx, const Resource::AssetId& id, const Ptr<Resource::ImageAsset>& img)
    {
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

        m_pool.emplace(id, PoolEntry{ tex, m_gcGeneration });
        return tex;
    }
}
