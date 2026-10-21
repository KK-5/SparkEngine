#pragma once

#include <cstdint>

#include <Drawable/DrawTag.h>

namespace Spark::Render
{
    //! Added by .Draw<Tag>(): the pass draws the instances whose draw mask has this bit, in
    //! the order and culled where Tag declares, a DrawGroup at a time (DrawList/DrawGroup.h).
    //! Read by DrawGroupSystem, which makes the groups, and the render graph, which draws them.
    struct PassDrawList
    {
        uint32_t    m_drawMaskBitIndex = 0;
        DrawOrder   m_order            = DrawOrder::Any;
        DrawCulling m_culling          = DrawCulling::Gpu;
    };
}
