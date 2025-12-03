#pragma once

#include <cstdint>

namespace Spark::Render::RHI
{
    enum class VendorId: uint32_t
    {
        Unknown  =    0,
        Intel    =    0x8086,
        NVidia   =    0x10de,
        AMD      =    0x1002,
        Qualcomm =    0x5143,
        Samsung  =    0x1099,
        ARM      =    0x13B5,
        Warp     =    0x1414,
        Apple    =    0x106B
    };
}