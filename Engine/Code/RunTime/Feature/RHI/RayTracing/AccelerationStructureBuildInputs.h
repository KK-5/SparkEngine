#pragma once

#include <EASTL/span.h>

#include <RHI/Format.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>
#include <RHI/RayTracing/AccelerationStructure.h>

namespace Spark::RHI
{
    class Device;

    //! Triangles.
    struct RayTracingGeometry
    {
        const Buffer* m_vertexBuffer = nullptr;
        uint64_t      m_vertexOffset = 0;
        uint32_t      m_vertexStride = 0;
        //! One the device reports FormatCapabilities::AccelerationStructureVertex for.
        Format        m_vertexFormat = Format::Unknown;
        uint32_t      m_vertexCount  = 0;
        //! Unknown for none, every three vertices a triangle.
        IndexFormat   m_indexFormat  = IndexFormat::Unknown;
        const Buffer* m_indexBuffer  = nullptr;
        uint64_t      m_indexOffset  = 0;
        uint32_t      m_indexCount   = 0;
        bool          m_opaque       = true;
    };

    inline uint32_t GetTriangleCount(const RayTracingGeometry& geometry)
    {
        return (geometry.m_indexFormat == IndexFormat::Unknown ? geometry.m_vertexCount : geometry.m_indexCount) / 3;
    }

    enum class AccelerationStructureBuildFlags : uint8_t
    {
        None            = 0,
        PreferFastTrace = BIT(0),
        PreferFastBuild = BIT(1),
        //! Not yet supported.
        AllowUpdate     = BIT(2),
        //! Not yet supported.
        AllowCompaction = BIT(3),
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::AccelerationStructureBuildFlags, uint8_t);

    //! What a build is made of, for the size query as for the build. The size query reads neither
    //! buffers nor offsets.
    struct AccelerationStructureBuildInputs
    {
        AccelerationStructureType             m_type  = AccelerationStructureType::BottomLevel;
        AccelerationStructureBuildFlags       m_flags = AccelerationStructureBuildFlags::None;
        //! Bottom level only.
        eastl::span<const RayTracingGeometry> m_geometries;
        //! Top level only: m_instanceCount RayTracingInstance from m_instanceOffset.
        const Buffer*                         m_instanceBuffer = nullptr;
        uint64_t                              m_instanceOffset = 0;
        uint32_t                              m_instanceCount  = 0;
    };

    //! Enough for any build of inputs with the same geometry count, type, flags, vertex formats and
    //! index formats, and no more triangles in any geometry or instances than queried with.
    struct AccelerationStructureBuildSizes
    {
        uint64_t m_resultSize        = 0;
        uint64_t m_buildScratchSize  = 0;
        uint64_t m_updateScratchSize = 0;
    };

    //! Checks what the size query reads.
    bool ValidateAccelerationStructureBuildInputs(const Device& device, const AccelerationStructureBuildInputs& inputs);
}
