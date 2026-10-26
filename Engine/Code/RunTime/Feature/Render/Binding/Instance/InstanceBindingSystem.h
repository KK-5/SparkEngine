#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Geometry/MeshGeometry.h>
#include <Transform/Components.h>

#include <DrawList/DrawListKeyCounter.h>
#include <Pass/PassContext.h>
#include <Staging/ElementUpdateBuffer.h>
#include <Suballocation/ReservedBuffer.h>

#include "InstanceBinding.h"

namespace Spark::Render
{
    //! Owns the g_Instances array (space4) + its ShaderBindings, and the per-instance ID
    //! vertex stream ([0..slot count)) that delivers InstanceIndex via PER_INSTANCE_DATA +
    //! StartInstanceLocation.
    //!
    //! Slot allocation, encoding and upload of g_Instances are all GlobalBuffer's; this only
    //! supplies the space4 SRG, the ID stream, and the per-renderable encode. The ID stream is
    //! a reserved buffer backed page by page as the slots reach it, the IDs of new slots
    //! written in a pass of its own.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after ViewBindingSystem
    //! and before the draw lists are culled and grouped.
    class InstanceBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Registers the pass writing the IDs, ahead of every pass that draws meshes.
        void SetUp(PassContext& passContext);
        //! frameIndex is the in-flight slot (Device::GetFrameIndex), used to
        //! pick this frame's g_Instances copy. RenderSystem::OnTick passes it.
        void Update(uint32_t frameIndex);
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound. Overflow logs an error and drops the surplus entities
        //! (rendering is not blocked). At 208 B per instance this is 13 MB (g_Instances) +
        //! 256 KB (ID buffer).
        static constexpr uint32_t Capacity = 65536;
        static constexpr uint64_t InstanceIdByteCount = uint64_t(Capacity) * sizeof(uint32_t);

        //! The IDs of the slots past those written, on the pages they reach.
        void UpdateInstanceIds(uint32_t slotCount);

        GlobalBuffer<Instances, InstanceData,
                     Transform::WorldTransformMatrix, MeshGeometry> m_instances;

        RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;  // Components::ShaderBindings — g_Instances @ space4
        RHI::RHIHandle m_slotCountEntity = RHI::NullHandle; // InstanceSlotCount — how far g_Instances is in use

        //! Mapped as far as the IDs reach.
        ReservedBuffer                          m_instanceIdPool;
        eastl::vector<Suballocator::PageChange> m_instanceIdPageChanges;
        //! The pool's buffer as the graph imports it.
        RHI::RHIHandle                          m_instanceIdResource = RHI::NullHandle;
        ElementUpdateBuffer                     m_instanceIdUpdate;
        //! IDs [0, this) are in the pool.
        uint32_t                                m_instanceIdCount = 0;

        // This frame's encoded instances per key and draw-mask bit, published as DrawListKeys.
        DrawListKeyCounter m_keyCounter;
    };
}
