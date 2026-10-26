#pragma once

#include <EASTL/vector.h>

#include <ECS/WorldContext.h>
#include <RHI/Context/RHIContext.h>

#include <Geometry/MeshGeometry.h>
#include <Transform/Components.h>

#include <DrawList/DrawListKeyCounter.h>
#include <Pass/PassContext.h>

#include "ActiveInstanceList.h"
#include "InstanceBinding.h"
#include "InstancePool.h"

namespace Spark::Render
{
    //! Owns the g_Instances array (space4) + its ShaderBindings, and the instance IDs: the
    //! active list, position -> slot, which a draw's FirstInstance indexes as the per-instance
    //! vertex stream and which delivers the slot as InstanceIndex.
    //!
    //! A renderable's slot is an InstanceSlot, a record of InstancePool, which writes the
    //! records that changed. A renderable's position is its place in the InstanceSlot storage,
    //! which entt keeps packed; ActiveInstanceList puts the list on the GPU. A pass of this
    //! system applies the writes of each.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked after ViewBindingSystem
    //! and before the draw lists are culled and grouped.
    class InstanceBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Registers the passes writing the records and the IDs, ahead of every pass that reads them.
        void SetUp(PassContext& passContext);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! A slot for each renderable with a transform and geometry; one that lost either gives its back.
        void AllocateSlots(WorldContext& world);

        //! Every held slot's record, into the pool.
        void UpdateInstanceData(WorldContext& world);

        //! This frame's active list into m_activeSlots.
        void CollectActiveSlots(WorldContext& world);

        RHI::RHIHandle m_bindingsEntity      = RHI::NullHandle;  // Components::ShaderBindings — g_Instances @ space4
        RHI::RHIHandle m_instanceCountEntity = RHI::NullHandle;  // ActiveInstanceCount

        InstancePool            m_instancePool;
        ActiveInstanceList      m_activeList;
        //! This frame's active list: position -> slot.
        eastl::vector<uint32_t> m_activeSlots;

        // This frame's encoded instances per key and draw-mask bit, published as DrawListKeys.
        DrawListKeyCounter m_keyCounter;
    };
}
