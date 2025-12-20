#pragma once

#include <Math/Bit.h>

namespace Spark::RHI
{
    enum class BufferBindFlags : uint32_t   
    {
        None                  = 0,
        InputAssembly         = BIT(0),
        DynamicInputAssembly  = BIT(1),
        Constant              = BIT(2),
        ShaderRead            = BIT(3),
        ShaderWrite           = BIT(4),
        ShaderReadWrite       = ShaderRead | ShaderWrite,
        CopyRead              = BIT(5),
        CopyWrite             = BIT(6),
        Predication           = BIT(7),
        Indirect              = BIT(8),

        RayTracingAccelerationStructure = BIT(9),
        RayTracingShaderTable           = BIT(10),
        RayTracingScratchBuffer         = BIT(11),
    };
    
}