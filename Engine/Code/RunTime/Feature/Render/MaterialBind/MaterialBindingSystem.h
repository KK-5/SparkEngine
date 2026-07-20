#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include "MaterialData.h"

namespace Spark::Render
{
    //! Owns the g_Materials StructuredBuffer (space3) + its ShaderBindings. Each frame
    //! it densely scatters every material's Material::MaterialParams into the current
    //! frame's g_Materials copy and stamps each material entity with its MaterialGPUSlot
    //! (the slot InstanceBindingSystem then bakes into InstanceData.m_materialIndex).
    //!
    //! Host per-frame + full re-scatter every frame, symmetric with InstanceBindingSystem
    //! (no device/dirty, no stable-slot table — materials are few, KB-level). Simpler
    //! than InstanceBindingSystem: no per-instance ID vertex stream, no draw-time slot
    //! table (the slot is consumed once, at InstanceBindingSystem scatter time).
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked BEFORE
    //! InstanceBindingSystem (which reads the MaterialGPUSlot this writes).
    class MaterialBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! frameIndex is the in-flight slot (swap-chain image index), picking this
        //! frame's g_Materials copy — same frame-index contract as InstanceBindingSystem.
        void Update(uint32_t frameIndex);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound on live materials. Overflow logs and drops the surplus
        //! (rendering is not blocked). 1024 * 32B = 32 KB per frame copy — trivially cheap.
        static constexpr uint32_t Capacity = 1024;

        //! Binds frameIndex's g_Materials copy as the structured SRV, every frame.
        //! Returns false until the ECS materializes BufferPerFrame (one warmup frame after
        //! Init — same deferred PendingBufferInit path as g_Instances; do NOT "optimize"
        //! into a synchronous Init create).
        bool BindFrameMaterials(uint32_t frameIndex);

        // Shared resources, owned by their RHIContext entities (this system holds handles).
        RHI::RHIHandle m_buffer   = RHI::NullHandle;  // Components::BufferPerFrame — host StructuredBuffer<MaterialData>, N copies
        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Materials @ space3

        // CPU staging for g_Materials. Filled each frame, handed to the current frame's
        // copy via PendingBufferMap. Lives for the system's lifetime so the map source
        // stays valid (PendingBufferMap contract, like InstanceBindingSystem::m_instanceData).
        eastl::vector<MaterialData> m_materialData;
    };
}
