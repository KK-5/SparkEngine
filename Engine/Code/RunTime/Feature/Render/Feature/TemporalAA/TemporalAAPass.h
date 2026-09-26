#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Full-screen pass after SceneColor is complete: blends it with last frame's TemporalAA,
    //! reprojected by ResolvedVelocity, into TemporalAA — both this frame's result and next
    //! frame's history. Runs only while the main view has ViewTemporalAA.
    struct TemporalAAPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
