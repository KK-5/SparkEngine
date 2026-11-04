#pragma once

#include <Suballocation/Suballocator.h>
#include <RHI/RayTracing/AccelerationStructure.h>

#include "RayTracingGeometryBuildInput.h"

namespace Spark::Render
{
    //! A world entity's BLAS storage and the geometry input it was built from.
    struct RayTracingBottomLevel
    {
        RayTracingBottomLevel() = default;
        RayTracingBottomLevel(RayTracingBottomLevel&&) noexcept = default;
        RayTracingBottomLevel& operator=(RayTracingBottomLevel&& other) noexcept;
        ~RayTracingBottomLevel();

        RHI::RHIHandle                m_bufferResource = RHI::NullHandle;
        Suballocation                 m_storage;
        RayTracingGeometryBuildInput m_input;
        Ptr<RHI::AccelerationStructure> m_structure;
    };

    //! Drops BLAS state whose geometry input disappeared or changed.
    void RemoveStaleRayTracingBottomLevels();
}
