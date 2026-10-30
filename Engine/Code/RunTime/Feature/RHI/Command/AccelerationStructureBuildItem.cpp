#include "AccelerationStructureBuildItem.h"

#include <Log/ILogSystem.h>
#include <RHI/RHILimits.h>
#include <RHI/Device/Device.h>
#include <RHI/RayTracing/RayTracingInstance.h>

namespace Spark::RHI
{
    namespace
    {
        bool ValidateBuildInput(const Buffer* buffer, uint64_t offset, uint64_t byteCount, uint32_t alignment, const char* what)
        {
            if (buffer == nullptr || !buffer->IsInitialized())
            {
                LOG_ERROR("[AccelerationStructureBuild] The {} buffer is null or not initialized.", what);
                return false;
            }

            const BufferDescriptor& descriptor = buffer->GetDescriptor();
            if (!CheckBitsAny(descriptor.m_bindFlags, BufferBindFlags::RayTracingBuildInput))
            {
                LOG_ERROR("[AccelerationStructureBuild] The {} buffer {} is not bound with RayTracingBuildInput.",
                          what, buffer->GetName().GetCStr());
                return false;
            }

            if (offset % alignment != 0)
            {
                LOG_ERROR("[AccelerationStructureBuild] The {} offset {} in {} is not a multiple of {}.",
                          what, offset, buffer->GetName().GetCStr(), alignment);
                return false;
            }

            if (offset + byteCount > descriptor.m_byteCount)
            {
                LOG_ERROR("[AccelerationStructureBuild] The {} range [{}, +{}) exceeds buffer {} of {} bytes.",
                          what, offset, byteCount, buffer->GetName().GetCStr(), descriptor.m_byteCount);
                return false;
            }

            return true;
        }

        bool ValidateGeometryBuffers(const RayTracingGeometry& geometry)
        {
            const uint32_t vertexAlignment = GetFormatComponentAlignment(geometry.m_vertexFormat);
            if (geometry.m_vertexStride == 0 || geometry.m_vertexStride % vertexAlignment != 0)
            {
                LOG_ERROR("[AccelerationStructureBuild] Vertex stride {} is not a nonzero multiple of {}.",
                          geometry.m_vertexStride, vertexAlignment);
                return false;
            }

            const uint64_t vertexBytes = uint64_t(geometry.m_vertexCount - 1) * geometry.m_vertexStride
                                       + GetFormatSize(geometry.m_vertexFormat);
            if (!ValidateBuildInput(geometry.m_vertexBuffer, geometry.m_vertexOffset, vertexBytes, vertexAlignment, "vertex"))
            {
                return false;
            }

            if (geometry.m_indexFormat == IndexFormat::Unknown)
            {
                if (geometry.m_indexBuffer != nullptr)
                {
                    LOG_ERROR("[AccelerationStructureBuild] A geometry without an index format takes no index buffer.");
                    return false;
                }
                return true;
            }

            const uint32_t indexSize = GetIndexFormatSize(geometry.m_indexFormat);
            return ValidateBuildInput(geometry.m_indexBuffer, geometry.m_indexOffset,
                                      uint64_t(geometry.m_indexCount) * indexSize, indexSize, "index");
        }
    }

    bool ValidateAccelerationStructureBuildItem(const Device& device, const AccelerationStructureBuildItem& item)
    {
        const AccelerationStructureBuildInputs& inputs = item.m_inputs;
        if (!ValidateAccelerationStructureBuildInputs(device, inputs))
        {
            return false;
        }

        if (item.m_mode != AccelerationStructureBuildMode::Build || item.m_source != nullptr)
        {
            LOG_ERROR("[AccelerationStructureBuild] Updates are not supported yet.");
            return false;
        }

        const AccelerationStructure* destination = item.m_destination;
        if (destination == nullptr || !destination->IsInitialized())
        {
            LOG_ERROR("[AccelerationStructureBuild] The destination is null or not initialized.");
            return false;
        }

        if (destination->GetType() != inputs.m_type)
        {
            LOG_ERROR("[AccelerationStructureBuild] The destination's type is not the inputs'.");
            return false;
        }

        if (inputs.m_type == AccelerationStructureType::BottomLevel)
        {
            for (const RayTracingGeometry& geometry : inputs.m_geometries)
            {
                if (!ValidateGeometryBuffers(geometry))
                {
                    return false;
                }
            }
        }
        else if (inputs.m_instanceCount != 0
                 && !ValidateBuildInput(inputs.m_instanceBuffer, inputs.m_instanceOffset,
                                        uint64_t(inputs.m_instanceCount) * sizeof(RayTracingInstance),
                                        Alignment::AccelerationStructureInstance, "instance"))
        {
            return false;
        }

        const Buffer* scratch = item.m_scratchBuffer;
        if (scratch == nullptr || !scratch->IsInitialized()
            || !CheckBitsAny(scratch->GetDescriptor().m_bindFlags, BufferBindFlags::RayTracingScratchBuffer))
        {
            LOG_ERROR("[AccelerationStructureBuild] The scratch buffer is null, not initialized or not bound with "
                      "RayTracingScratchBuffer.");
            return false;
        }

        const uint32_t scratchAlignment = device.GetLimits().m_accelerationStructureScratchOffsetAlignment;
        if (item.m_scratchOffset % scratchAlignment != 0)
        {
            LOG_ERROR("[AccelerationStructureBuild] Scratch offset {} is not a multiple of {}.",
                      item.m_scratchOffset, scratchAlignment);
            return false;
        }

        const AccelerationStructureBuildSizes sizes = device.GetAccelerationStructureBuildSizes(inputs);
        if (destination->GetSize() < sizes.m_resultSize)
        {
            LOG_ERROR("[AccelerationStructureBuild] The destination's {} bytes are fewer than the {} the build needs.",
                      destination->GetSize(), sizes.m_resultSize);
            return false;
        }

        const uint64_t scratchByteCount = scratch->GetDescriptor().m_byteCount;
        if (item.m_scratchOffset > scratchByteCount || scratchByteCount - item.m_scratchOffset < sizes.m_buildScratchSize)
        {
            LOG_ERROR("[AccelerationStructureBuild] Scratch {} from offset {} holds fewer than the {} bytes the build needs.",
                      scratch->GetName().GetCStr(), item.m_scratchOffset, sizes.m_buildScratchSize);
            return false;
        }

        return true;
    }
}
