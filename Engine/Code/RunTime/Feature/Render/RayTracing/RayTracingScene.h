#pragma once

#include <cstdint>

#include <EASTL/array.h>
#include <RHI/RayTracing/AccelerationStructure.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

namespace Spark::Render
{
    inline constexpr uint32_t RayTracingInstanceCapacity = 65535;

    //! The shader input the TLAS is published at (space 7): one name for the group's layout
    //! and its publication.
    inline constexpr const char* TopLevelSceneInputName = "g_RayTracingScene";

    //! On the shared storage buffer once this frame's TLAS build is scheduled.
    struct RayTracingSceneTag {};

    //! Both TLAS objects refer to disjoint ranges of this resource's shared AS buffer.
    //! A pass that binds both imports the buffer once and binds each range explicitly.
    struct RayTracingScene
    {
        struct TopLevel
        {
            RHI::BufferViewDescriptor m_view;
            Ptr<RHI::AccelerationStructure> m_structure;
        };

        const TopLevel* GetCurrentTopLevel() const
        {
            return m_currentReady ? &m_topLevels[m_currentSlot] : nullptr;
        }

        const TopLevel* GetPreviousTopLevel() const
        {
            return m_previousReady ? &m_topLevels[1 - m_currentSlot] : nullptr;
        }

    private:
        friend class RayTracingSceneSystem;
        static constexpr uint32_t TopLevelSlotCount = 2;

        eastl::array<TopLevel, TopLevelSlotCount> m_topLevels;
        uint32_t m_currentSlot = 0;
        bool m_currentReady = false;
        bool m_previousReady = false;
    };
}
