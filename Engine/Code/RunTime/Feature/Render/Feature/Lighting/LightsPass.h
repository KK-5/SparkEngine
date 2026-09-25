#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Direct lighting: a full-screen triangle that decodes the GBuffer and accumulates every
    //! analytic light's Cook-Torrance response into SceneColor, attenuated by ShadowMask.
    //! First of the three deferred lighting passes, alongside IndirectDiffusePass and
    //! ReflectionsPass -- all three blend additively into the same target, so their order
    //! among themselves does not matter.
    //!
    //! Its one Scope binds the GBuffer and depth to its shader inputs and draws a full-screen
    //! triangle.
    struct LightsPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
