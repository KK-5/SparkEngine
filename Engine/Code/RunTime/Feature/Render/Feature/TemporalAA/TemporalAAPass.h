#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class PassContext;
    struct ViewTemporalAA;

    //! Full-screen pass after SceneColor is complete: blends it with last frame's TemporalAA,
    //! reprojected by ResolvedVelocity, into TemporalAA — both this frame's result and next
    //! frame's history. Runs only while the main view has ViewTemporalAA.
    struct TemporalAAPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);

        //! The main view's settings, or null when it does not run temporal AA. Single main
        //! view for now.
        static const ViewTemporalAA* FindMainViewSettings(RHI::RHIContext& ctx);
    };
}
