#pragma once

#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    //! GBuffer-based 1 spp direct lighting. Produces separate diffuse/specular
    //! signals and denoising guides, then composes them over SceneColor's emission.
    //! Requires ray tracing. No indirect lighting or temporal accumulation.
    struct DirectLightingPass
    {
        static void SetUp(PassContext& ctx);
    };
}
