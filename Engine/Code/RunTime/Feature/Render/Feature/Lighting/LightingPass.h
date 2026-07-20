#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Deferred lighting pass. A full-screen triangle that samples the GBuffer
    //! (Albedo / Normal / ORM / Position) and shades a single hardcoded directional
    //! light with a Cook-Torrance BRDF, writing SceneColor. Runs after GBufferPass
    //! and before SkyboxPass (which fills the pixels this pass discards).
    //!
    //! The GBuffer views are bound to this pass's own space2 SRG in the .Compile()
    //! hook — the phase after transient resources are materialized but before shader
    //! inputs are compiled — via FindPassAttachmentImageView + SetPassShaderImage.
    //! The SRG itself is created and injected into the draw request by
    //! LightingProcessor during Process.
    struct LightingPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
