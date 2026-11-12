#pragma once

#include <cstdint>

#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

namespace Spark::Render
{
    inline constexpr uint32_t RayTracingInstanceCapacity = 65535;

    //! The shader input the TLAS is published at (space 7): one name for the group's layout
    //! and its publication.
    inline constexpr const char* TopLevelSceneInputName = "g_RayTracingScene";

    //! On the shared storage buffer once this frame's TLAS build is scheduled.
    struct RayTracingSceneTag {};

    //! The TLAS range in the storage buffer. The structure is the buffer entity's
    //! AccelerationStructure component, which is also what shaders are bound to.
    struct RayTracingScene
    {
        RHI::BufferViewDescriptor m_topLevelView;
    };
}
