#pragma once

#include <EASTL/unordered_map.h>

#include <RHI/Context/RHIContext.h>
#include <Resource/AssetTypes.h>

namespace Spark::Resource { class ImageAsset; }

namespace Spark::Render
{
    //! Persistent AssetId->RHIHandle texture pool + per-frame resolver. Reads each
    //! material's authored base-color AssetId, makes the GPU texture resident (upload +
    //! pass-independent static-import barrier) and writes MaterialGPUTextures{RHIHandle}
    //! on the material entity. Owned by RenderSystem, ticked BEFORE MaterialBindingSystem
    //! (which turns the RHIHandle into a bindless index). See TODO_MaterialSystemPlan.md B.7.
    class MaterialTextureSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        void CollectGarbage();
        void Shutdown(RHI::RHIContext& rhiCtx);

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
