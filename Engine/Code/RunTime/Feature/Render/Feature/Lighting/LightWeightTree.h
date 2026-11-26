#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

namespace Spark::Render::LightWeightTree
{
    inline constexpr char BufferName[] = "LightWeightTree";

    //! The root is at 1. Leaves start at m_leafBase, in scene light slot then emissive-list order.
    struct Layout
    {
        uint32_t m_sceneLightSlotCount = 0;
        uint32_t m_emissiveInstanceCount = 0;
        uint32_t m_leafBase = 0;

        uint32_t LeafCount() const { return m_sceneLightSlotCount + m_emissiveInstanceCount; }
        uint32_t NodeCount() const { return 2 * m_leafBase; }
    };

    //! Both passes size their views from the scene light slot bound and emissive instance count.
    Layout LayOut(RHI::RHIContext& context);
}
