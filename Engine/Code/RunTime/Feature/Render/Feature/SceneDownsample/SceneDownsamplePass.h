#pragma once

#include <Base.h>

namespace Spark::Render
{
    class PassContext;

    //! Halves the finished HDR scene color level by level into the scene downsample chain, one
    //! Scope per level (PostProcess::SceneDownsampleChain). The chain is shared: bloom builds on
    //! it, and later the exposure histogram reads one of its levels, so it carries no threshold.
    struct SceneDownsamplePass
    {
        static void SetUp(PassContext& ctx);
    };
}
