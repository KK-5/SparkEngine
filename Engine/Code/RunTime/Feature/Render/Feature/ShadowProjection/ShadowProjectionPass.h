#pragma once

#include <Base.h>
#include <RHI/Context/RHIContext.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Slices the projection writes this frame; 0 when it produces no mask at all -- no
    //! shadow-casting light was granted a slot, or the atlas is not resident yet. LightingPass
    //! gates its read on the same answer, so the two cannot disagree about whether the
    //! attachment exists.
    uint32_t ShadowMaskSliceCount(RHI::RHIContext& ctx);

    //! Resolves the shadow atlas into ShadowMask, the screen-space visibility signal the
    //! lighting passes read. Runs between GBufferPass and the lighting passes.
    //!
    //! One instance per mask slice, each writing the four lights packed into it, selected by
    //! SV_RenderTargetArrayIndex. The draw comes from ShadowMaskSystem, which owns its
    //! DrawItem outright -- its instance count follows the light set, so there is nothing to
    //! derive once -- and stamps it with this pass's PassTag.
    struct ShadowProjectionPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
