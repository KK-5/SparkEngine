#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

namespace Spark::Render::LightWeightTree
{
    inline constexpr char BufferName[] = "LightWeightTree";

    //! The root is at 1. Leaves start at m_leafBase, indexed by light slot.
    struct Layout
    {
        uint32_t m_lightSlotCount = 0;
        uint32_t m_leafBase = 0;

        uint32_t NodeCount() const { return 2 * m_leafBase; }
    };

    //! Consumers size their views from the current light slot bound.
    Layout LayOut(RHI::RHIContext& context);
}
