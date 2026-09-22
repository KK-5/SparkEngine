#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Indirect specular: a full-screen triangle that adds the prefiltered environment
    //! through the split-sum approximation to SceneColor. Separate from IndirectDiffusePass
    //! because reflections take a different route -- SSR, then ray tracing -- and will want
    //! their own resolution.
    //!
    //! Blends additively into SceneColor like the other two lighting passes, so their order
    //! among themselves does not matter.
    struct ReflectionsPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
