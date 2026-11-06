#pragma once

#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

namespace Spark::Render
{
    //! On a world entity whose geometry is ready for a BLAS build this frame.
    struct RayTracingGeometryBuildInput
    {
        RHI::RHIHandle            m_vertexBuffer = RHI::NullHandle;
        RHI::BufferViewDescriptor m_vertexView;
        RHI::Format               m_vertexFormat = RHI::Format::Unknown;

        //! Null for non-indexed geometry; otherwise an R16_UINT or R32_UINT view.
        RHI::RHIHandle            m_indexBuffer = RHI::NullHandle;
        RHI::BufferViewDescriptor m_indexView;
        bool                      m_opaque = true;

        bool operator==(const RayTracingGeometryBuildInput& other) const
        {
            return m_vertexBuffer == other.m_vertexBuffer
                && m_vertexView == other.m_vertexView
                && m_vertexFormat == other.m_vertexFormat
                && m_indexBuffer == other.m_indexBuffer
                && m_indexView == other.m_indexView
                && m_opaque == other.m_opaque;
        }

        bool operator!=(const RayTracingGeometryBuildInput& other) const
        {
            return !(*this == other);
        }
    };
}
