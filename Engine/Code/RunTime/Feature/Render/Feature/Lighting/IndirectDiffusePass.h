#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Indirect diffuse: a full-screen triangle that adds the environment's irradiance to
    //! SceneColor, or a constant ambient when nothing is baked. Separate from LightsPass
    //! because its source is the one that gets replaced -- DDGI, later SSGI -- while the
    //! analytic lights stay put.
    //!
    //! Blends additively into SceneColor like the other two lighting passes, so their order
    //! among themselves does not matter.
    struct IndirectDiffusePass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
