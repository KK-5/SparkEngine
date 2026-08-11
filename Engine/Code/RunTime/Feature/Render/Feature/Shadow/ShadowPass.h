#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Renders every shadow view into the one atlas. One pass for all of them:
    //! .RendersView<ShadowViewTag>() replays its draws per view, each under that view's tile
    //! viewport and space1 SRG.
    struct ShadowPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
