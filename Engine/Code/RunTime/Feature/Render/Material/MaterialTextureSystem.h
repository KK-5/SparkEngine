#pragma once

#include <EASTL/unordered_map.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Context/UniqueRHIHandle.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource { class ImageAsset; }
namespace Spark::RHI { class Device; class ImagePool; }

namespace Spark::Render
{
    //! Renderer-owned material textures, deduplicated by asset identity. One pass over
    //! live materials resolves their slots and marks cache entries before sweeping them.
    class MaterialTextureSystem
    {
    public:
        MaterialTextureSystem();
        ~MaterialTextureSystem();

        void Init(RHI::Device& device);
        void Update();
        //! Retires handles; the common reaper runs before this object's image pool goes.
        void Shutdown();

    private:
        //! The cache lookup already missed. Creates an image and submits one upload.
        RHI::RHIHandle CreateTexture(RHI::RHIContext& rhiCtx, const Resource::AssetId& id,
            const Ptr<Resource::ImageAsset>& image);

        struct TextureEntry
        {
            RHI::UniqueRHIHandle m_handle;
            uint32_t m_generation = 0;
        };

        Ptr<RHI::ImagePool> m_imagePool;
        eastl::unordered_map<Resource::AssetId, TextureEntry> m_textures;
        uint32_t m_generation = 0;
    };
}
