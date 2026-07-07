#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Deferred geometry pass (the "base pass"). Depth-tests read-only against
    //! DepthPrePass's SceneDepth with ComparisonFunc::Equal (early-Z: only the
    //! fragments that own the depth get shaded), and writes the GBuffer MRT —
    //! Albedo / Normal / ORM — for the deferred lighting pass to sample. Material
    //! is hardcoded until the material system lands.
    struct GBufferPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
