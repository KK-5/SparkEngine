#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Final tonemap / present pass. A full-screen triangle that samples the linear-HDR
    //! scene color, blends in the bloom, applies exposure, AgX with the view's look and gamma,
    //! and writes the LDR swap chain. It is the pivot from linear-HDR scene space to
    //! display-referred LDR.
    //!
    //! It imports the swap chain as its color render target; UIPass draws on top of the
    //! tonemapped result afterwards.
    struct TonemapPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
