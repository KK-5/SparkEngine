#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Mesh/Components.h>
#include <Transform/Components.h>

#include "InstanceBinding.h"

namespace Spark::Render
{
    //! Owns the g_Instances array (space4) + its ShaderBindings, and the static
    //! per-instance ID vertex stream ([0..Capacity-1]) that delivers InstanceIndex via
    //! PER_INSTANCE_DATA + StartInstanceLocation.
    //!
    //! Slot allocation, encoding and upload are all GlobalBuffer's; this only supplies
    //! the space4 SRG, the ID stream, and the per-renderable encode.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after ViewBindingSystem
    //! and before DrawItem derivation (MeshGeometryComposer / DrawItemRouter).
    class InstanceBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! frameIndex is the in-flight slot (swap-chain GetCurrentImageIndex), used to
        //! pick this frame's g_Instances copy. RenderSystem::OnTick passes it.
        void Update(uint32_t frameIndex);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound. Overflow logs an error and drops the surplus entities
        //! (rendering is not blocked). At Model-only size this is 9 MB (g_Instances) +
        //! 256 KB (ID buffer).
        static constexpr uint32_t Capacity = 65536;

        GlobalBuffer<Instances, InstanceData,
                     Transform::WorldTransformMatrix, Mesh::MeshGPUComponent> m_instances;

        RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;  // Components::ShaderBindings — g_Instances @ space4
        RHI::RHIHandle m_idBufferEntity = RHI::NullHandle;  // Components::Buffer — per-instance vertex stream ([0..Capacity-1])

        // CPU-side source for the one-time ID-buffer upload. Must outlive the async
        // upload (PendingBufferUpload contract), so it lives for the system's lifetime.
        eastl::vector<uint32_t> m_idData;
    };
}
