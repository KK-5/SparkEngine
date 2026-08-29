#pragma once

#include <EASTL/unordered_map.h>

#include <RHI/Context/RHIContext.h>
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
    //! each RHIHandle into a bindless index. The static-import attachment (a render-graph
    //! usage concept) is registered by the render consumer, not here.
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

        struct PoolEntry
        {
            RHI::RHIHandle m_handle = RHI::NullHandle;
            uint32_t       m_gen    = 0;
        };

        eastl::unordered_map<Resource::AssetId, PoolEntry> m_pool;
        uint32_t m_gcGeneration = 0;
    };
}
