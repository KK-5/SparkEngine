#pragma once

#include <Object/Object.h>

namespace Spark::RHI
{
    using ShaderInputIndex = uint32_t;
    using ShaderInputName = ObjectName;

    static constexpr ShaderInputIndex InvalidShaderInputIndex = static_cast<uint32_t>(-1);
    static constexpr uint32_t InvalidBindingSlot = static_cast<uint32_t>(-1);
}