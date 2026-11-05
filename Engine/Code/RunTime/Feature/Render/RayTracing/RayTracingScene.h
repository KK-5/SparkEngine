#pragma once

#include <cstdint>

#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/RayTracing/AccelerationStructure.h>

namespace Spark::Render
{
    inline constexpr uint32_t RayTracingInstanceCapacity = 65535;

    //! On the shared storage buffer once this frame's TLAS build is scheduled.
    struct RayTracingSceneTag {};

    struct RayTracingScene
    {
        Ptr<RHI::AccelerationStructure> m_topLevel;
        RHI::BufferViewDescriptor m_topLevelView;
    };
}
