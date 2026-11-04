#pragma once

#include <cstdint>

#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

namespace Spark::Render
{
    inline constexpr uint32_t RayTracingInstanceCapacity = 65535;

    //! On the shared buffer storing the scene's BLASes and TLAS.
    struct RayTracingSceneTag {};

    struct RayTracingScene
    {
        RHI::BufferViewDescriptor m_topLevelView;
        //! TLAS contents are built or scheduled before readers this frame.
        bool m_ready = false;
    };
}
