#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Final tonemap / present pass. A full-screen triangle that samples the linear-HDR
    //! SceneColor, applies Reinhard + gamma, and writes the LDR swap chain. It is the
    //! pivot from linear-HDR scene space to display-referred LDR.
    //!
    //! This pass replaces CopyFrameBufferPass: it now IMPORTS the swap chain (as its
    //! color render target) and UIPass draws on top of the tonemapped result afterwards.
    //! The SceneColor SRV is bound to this pass's own space0 SRG in the .Compile() hook
    //! (once SceneColor is materialized); TonemapProcessor builds the draw request.
    struct TonemapPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
