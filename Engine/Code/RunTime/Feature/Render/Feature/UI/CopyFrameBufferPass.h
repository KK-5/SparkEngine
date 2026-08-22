#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! NOT WIRED — TonemapPass imports the swap chain directly and drew this out of the
    //! pipeline; SetUp has no caller, and the UIProcessFeature side that produced its
    //! CopyRequest was removed with it. Kept as the worked example of a copy pass and of
    //! the Execute-hook escape hatch: its hook hand-rolls the RENDER_TARGET <-> COPY_DEST
    //! round trip a clear needs, because the render graph has no declarative place for a
    //! copy pass to clear (loadOp is a render pass property, and neither backend permits a
    //! copy inside one).
    //!
    //! Re-wiring it means restoring a CopyRequest producer as well.
    struct CopyFrameBufferPass
    {
        static void SetUp(PassContext& ctx);
    };
}