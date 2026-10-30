#include "AccelerationStructureBuildInputs.h"

#include <Log/ILogSystem.h>
#include <RHI/Device/Device.h>

namespace Spark::RHI
{
    namespace
    {
        bool ValidateGeometry(const Device& device, const RayTracingGeometry& geometry, size_t index)
        {
            if (!CheckBitsAny(device.GetFormatCapabilities(geometry.m_vertexFormat), FormatCapabilities::AccelerationStructureVertex))
            {
                LOG_ERROR("[AccelerationStructureBuild] Geometry {}: vertex format {} cannot be built from.",
                          index, static_cast<uint32_t>(geometry.m_vertexFormat));
                return false;
            }

            if (geometry.m_vertexCount == 0)
            {
                LOG_ERROR("[AccelerationStructureBuild] Geometry {} has no vertices.", index);
                return false;
            }

            if (geometry.m_indexFormat == IndexFormat::Unknown)
            {
                if (geometry.m_indexCount != 0 || geometry.m_vertexCount % 3 != 0)
                {
                    LOG_ERROR("[AccelerationStructureBuild] Geometry {} has no index format: it takes no indices, "
                              "and its {} vertices must be a multiple of 3.", index, geometry.m_vertexCount);
                    return false;
                }
            }
            else if (geometry.m_indexCount == 0 || geometry.m_indexCount % 3 != 0)
            {
                LOG_ERROR("[AccelerationStructureBuild] Geometry {}: {} indices, not a nonzero multiple of 3.",
                          index, geometry.m_indexCount);
                return false;
            }

            return true;
        }
    }

    bool ValidateAccelerationStructureBuildInputs(const Device& device, const AccelerationStructureBuildInputs& inputs)
    {
        if (CheckBitsAny(inputs.m_flags, AccelerationStructureBuildFlags::AllowUpdate | AccelerationStructureBuildFlags::AllowCompaction))
        {
            LOG_ERROR("[AccelerationStructureBuild] AllowUpdate and AllowCompaction are not supported yet.");
            return false;
        }

        if (CheckBitsAll(inputs.m_flags, AccelerationStructureBuildFlags::PreferFastTrace | AccelerationStructureBuildFlags::PreferFastBuild))
        {
            LOG_ERROR("[AccelerationStructureBuild] PreferFastTrace and PreferFastBuild exclude each other.");
            return false;
        }

        if (inputs.m_type == AccelerationStructureType::BottomLevel)
        {
            if (inputs.m_geometries.empty()
                || inputs.m_instanceBuffer != nullptr || inputs.m_instanceOffset != 0 || inputs.m_instanceCount != 0)
            {
                LOG_ERROR("[AccelerationStructureBuild] A bottom level build takes geometries and no instances.");
                return false;
            }

            for (size_t i = 0; i < inputs.m_geometries.size(); ++i)
            {
                if (!ValidateGeometry(device, inputs.m_geometries[i], i))
                {
                    return false;
                }
            }
        }
        else if (!inputs.m_geometries.empty())
        {
            LOG_ERROR("[AccelerationStructureBuild] A top level build takes instances and no geometries.");
            return false;
        }

        return true;
    }
}
