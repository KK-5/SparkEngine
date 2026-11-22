#pragma once

namespace Spark::Render
{
    class PassContext;

    //! GBuffer-based direct lighting: one light sample and one GGX sample per pixel.
    //! Uses specular MIS, outputs separate diffuse/specular signals and denoising
    //! guides, then composes them over SceneColor's emission.
    //! Requires ray tracing. No indirect lighting or temporal accumulation.
    struct DirectLightingPass
    {
        static void SetUp(PassContext& ctx);
        static void SetUpComposite(PassContext& ctx);
    };
}
