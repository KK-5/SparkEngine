#pragma once

#include <cstdint>

namespace Spark
{
    enum class MetaTypeTraits : uint8_t
    {
        None        = 0,
        Editable    = 1 << 0,
    };
}