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
    //! The GBuffer views are bound to this pass's own space2 SRG in the .Compile() hook -- the
    //! phase after transient resources are materialized but before shader inputs are compiled.
    //! Its DrawItem comes from the shared full-screen-triangle spec (FullScreenTriangleTag).
    struct LightsPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
