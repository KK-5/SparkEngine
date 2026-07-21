#pragma once

#include <EASTL/unordered_map.h>

#include <RHI/Context/RHIContext.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource { class ImageAsset; }

namespace Spark::Material
{
    //! Persistent AssetId->RHIHandle base-color texture pool + per-frame resolver.
    //! Reads each material's authored base-color AssetId, makes the GPU texture resident
    //! (create + upload) and writes MaterialGPUTextures{RHIHandle} on the material entity.
    //!
    //! Owned and driven by MaterialSystem (composition) — its tick runs BEFORE the render
    //! system, so MaterialGPUTextures is ready when render's MaterialBindingSystem turns
    //! the RHIHandle into a bindless index. The static-import attachment (a render-graph
    //! usage concept) is registered by the render consumer, not here.
    class MaterialTextureSystem
    {
    public:
        void Init();
        void Update();
        void CollectGarbage();
        void Shutdown();

    private:
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
