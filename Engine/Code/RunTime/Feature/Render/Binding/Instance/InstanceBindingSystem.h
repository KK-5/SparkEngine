#pragma once

#include <EASTL/vector.h>

#include <ECS/WorldContext.h>
#include <RHI/Context/RHIContext.h>

#include <Geometry/MeshGeometry.h>
#include <Transform/Components.h>

#include <Binding/StagedArrayBuffer.h>
#include <DrawList/DrawListKeyCounter.h>
#include <Pass/PassContext.h>

#include "ActiveInstanceList.h"
#include "InstanceBinding.h"

namespace Spark::Render
{
    //! Owns the g_Instances array (space4) + its ShaderBindings, and the instance IDs: the
    //! active list, position -> slot, which a draw's FirstInstance indexes as the per-instance
    //! vertex stream and which delivers the slot as InstanceIndex.
    //!
    //! A renderable's slot is an InstanceSlot, a range of an address space of InstanceCapacity
    //! records; g_Instances is rewritten whole every frame into its in-flight copy, the holes
    //! as InstanceData{}, which reads as nothing here. A renderable's position is its place in
    //! the InstanceSlot storage, which entt keeps packed; ActiveInstanceList puts the list on
    //! the GPU, and a pass of this system applies its writes.
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
        //! A slot for each renderable with a transform and geometry, none for one without.
        void UpdateSlots(WorldContext& world);

        //! Every slot's record, the holes cleared.
        void EncodeInstances(WorldContext& world);

        //! This frame's active list into m_activeSlots.
        void CollectActiveSlots(WorldContext& world);

        //! Slots [0, this) take in every live one.
        uint32_t GetSlotCount() const;

        Ptr<Suballocator>                m_slotAllocator;
        StagedArrayBuffer<InstanceData>  m_instances;

        RHI::RHIHandle m_bindingsEntity      = RHI::NullHandle;  // Components::ShaderBindings — g_Instances @ space4
        RHI::RHIHandle m_instanceCountEntity = RHI::NullHandle;  // ActiveInstanceCount

        ActiveInstanceList      m_activeList;
        //! This frame's active list: position -> slot.
        eastl::vector<uint32_t> m_activeSlots;

        // This frame's encoded instances per key and draw-mask bit, published as DrawListKeys.
        DrawListKeyCounter m_keyCounter;
    };
}
