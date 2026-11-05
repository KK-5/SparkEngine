#pragma once

#include <cstdint>
#include <Math/Bit.h>

namespace Spark::RHI
{
    //! The same values in DX12 and Vulkan.
    enum class RayTracingInstanceFlags : uint8_t
    {
        None                          = 0,
        TriangleCullDisable           = BIT(0),
        TriangleFrontCounterClockwise = BIT(1),
        ForceOpaque                   = BIT(2),
        ForceNonOpaque                = BIT(3),
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::RHI::RayTracingInstanceFlags, uint8_t);

    //! Bit for bit D3D12_RAYTRACING_INSTANCE_DESC and VkAccelerationStructureInstanceKHR: written
    //! straight into an instance buffer, tightly packed, by the CPU or a shader.
    struct RayTracingInstance
    {
        //! Row major, object to world.
        float    m_transform[3][4];
        // API layout: ID/offset in low 24 bits, mask/flags in high 8 bits.
        // C++ bit-field order is implementation-defined; assumes MSVC's low-to-high allocation.
        uint32_t m_instanceId : 24;
        uint32_t m_mask : 8;
        uint32_t m_hitGroupOffset : 24;
        //! RayTracingInstanceFlags.
        uint32_t m_flags : 8;
        //! AccelerationStructure::GetDeviceAddress of a bottom level structure.
        uint64_t m_accelerationStructureAddress;
    };

    static_assert(sizeof(RayTracingInstance) == 64);
}
