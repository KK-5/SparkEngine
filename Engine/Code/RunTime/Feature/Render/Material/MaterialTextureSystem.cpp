#include "MaterialTextureSystem.h"

#include <EASTL/string.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <RHI/Factory.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>
#include <Material/Components.h>
#include <Material/MaterialContext.h>

#include "MaterialTextureComponents.h"

#include <Binding/ShaderUsage.h>

namespace Spark::Render
{
    namespace
    {
        //! The upload request keeps its source alive even if all materials stop using it.
        struct MaterialTextureUploadSource
        {
            Ptr<Resource::ImageAsset> m_image;
        };
    }

    MaterialTextureSystem::MaterialTextureSystem() = default;

    MaterialTextureSystem::~MaterialTextureSystem()
    {
        if (m_imagePool)
        {
            m_imagePool->Shutdown();
        }
    }

    void MaterialTextureSystem::Init(RHI::Device& device)
    {
        RHI::ImagePoolDescriptor descriptor;
        descriptor.m_bindFlags = RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite;
        m_imagePool = Service<RHI::Factory>::Get()->CreateImagePool();
        m_imagePool->SetName(ObjectName("MaterialTextures"));
        if (m_imagePool->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[MaterialTextureSystem] Image pool init failed.");
            m_imagePool.reset();
        }
    }

    void MaterialTextureSystem::Update()
    {
        auto* matCtx = Material::MaterialExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!m_imagePool || !matCtx || !rhiCtx)
        {
            return;
        }
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ++m_generation;

        matCtx->GetView<Resource::StandardPBR>(Exclude<DeadTag>).each(
            [&](Material::MaterialHandle handle, const Resource::StandardPBR& params)
        {
            MaterialGPUTextures gpu;
            for (size_t slot = 0; slot < Resource::MaterialTexSlotCount; ++slot)
            {
                const auto& id = params.m_textures[slot];
                if (!id.IsValid())
                {
                    continue;
                }
                if (auto it = m_textures.find(id); it != m_textures.end())
                {
                    it->second.m_generation = m_generation;
                    gpu.m_handles[slot] = it->second.m_handle.Get();
                    continue;
                }

                const auto asset = assetManager ? assetManager->FindAsset(id) : Ptr<Resource::Asset>{};
                if (!asset || asset->GetAssetType() != Resource::AssetType::Image || !asset->IsReady())
                {
                    continue;
                }
                gpu.m_handles[slot] = CreateTexture(*rhiCtx, id,
                    Ptr<Resource::ImageAsset>(static_cast<Resource::ImageAsset*>(asset.get())));
            }
            matCtx->AddOrReplace<MaterialGPUTextures>(handle, gpu);
        });

        // The slot traversal above also marks usage, so no second material scan is needed.
        for (auto it = m_textures.begin(); it != m_textures.end();)
        {
            if (it->second.m_generation != m_generation)
            {
                it = m_textures.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void MaterialTextureSystem::Shutdown()
    {
        if (auto* matCtx = Material::MaterialExecuteContext::Current())
        {
            matCtx->Clear<MaterialGPUTextures>();
        }
        m_textures.clear();
    }

    RHI::RHIHandle MaterialTextureSystem::CreateTexture(
        RHI::RHIContext& rhiCtx, const Resource::AssetId& id, const Ptr<Resource::ImageAsset>& asset)
    {
        const auto* data = asset->GetImageData();
        if (!data || data->GetTextureBytes().empty())
        {
            LOG_ERROR("[MaterialTextureSystem] Texture '{}' has no image data.", id.GetPath().c_str());
            return RHI::NullHandle;
        }

        auto descriptor = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
            asset->GetWidth(), asset->GetHeight(), asset->GetFormat());
        descriptor.m_mipLevels = static_cast<uint16_t>(asset->GetMipLevels());
        descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        const ObjectName name(eastl::string("MaterialTex_") +
            eastl::to_string(static_cast<unsigned long long>(id.GetHash())));
        Ptr<RHI::Image> image = Service<RHI::Factory>::Get()->CreateImage();
        image->SetName(name);
        RHI::ImageInitRequest init;
        init.m_image = image.get();
        init.m_descriptor = descriptor;
        if (m_imagePool->InitImage(init) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[MaterialTextureSystem] Texture '{}' failed to initialize.", id.GetPath().c_str());
            return RHI::NullHandle;
        }

        const auto handle = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::ResourceName>(handle, RHI::ResourceName{ name });
        rhiCtx.Add<RHI::Components::Image>(handle, RHI::Components::Image{ eastl::move(image) });
        rhiCtx.Add<MaterialTextureTag>(handle);

        // Sampled through the bindless heap: no fixed input, and the view the material's
        // indices are resolved through is the whole image, as the sampling samples it.
        rhiCtx.Add<ShaderImageUsage>(handle, ShaderImageUsage{
            ShaderInputBinding::Bindless(), RHI::ImageViewDescriptor{} });

        const auto request = RHI::RequestImageUpload(rhiCtx, handle,
            data->GetTextureBytes().data(), data->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(descriptor), RHI::Origin(), asset->GetFormat());
        RHI::RHIEventExecuteContext::Current()->Add<MaterialTextureUploadSource>(
            request, MaterialTextureUploadSource{ asset });
        m_textures.emplace(id, TextureEntry{ RHI::UniqueRHIHandle(handle), m_generation });
        return handle;
    }
}
