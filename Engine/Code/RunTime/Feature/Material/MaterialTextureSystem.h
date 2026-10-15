#pragma once

#include <EASTL/unordered_map.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Context/UniqueRHIHandle.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource { class ImageAsset; }

namespace Spark::Material
{
    //! Persistent AssetId->RHIHandle texture pool + per-frame resolver. Walks every
    //! slot of each material's Resource::StandardPBR::m_textures, makes each authored
    //! texture resident (create + upload) and writes the resolved RHIHandles into
    //! MaterialGPUTextures::m_handles[slot] on the material entity.
    //!
    //! Owned and driven by MaterialSystem (composition) — its tick runs BEFORE the render
    //! system, so MaterialGPUTextures is ready when render's MaterialBindingSystem turns
    //! each RHIHandle into a bindless index once the texture's upload is over.
    class MaterialTextureSystem
    {
    public:
        void Init();
        void Update();
        void CollectGarbage();
        void Shutdown();

    private:
        //! Callers must have missed m_pool first: this creates and uploads unconditionally.
        RHI::RHIHandle EnsureResident(RHI::RHIContext& rhiCtx, const Resource::AssetId& id,
                                      const Ptr<Resource::ImageAsset>& img);

        //! Owns the texture: dropping the entry lets it go.
        struct PoolEntry
        {
            RHI::UniqueRHIHandle m_handle;
            uint32_t             m_gen = 0;
        };

        eastl::unordered_map<Resource::AssetId, PoolEntry> m_pool;
        uint32_t m_gcGeneration = 0;
    };
}
