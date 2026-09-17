#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Full-screen pass after GBufferPass: reads Velocity and SceneDepth, writes ResolvedVelocity,
    //! which has a value at every pixel. Temporal consumers read only ResolvedVelocity.
    struct VelocityResolvePass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
