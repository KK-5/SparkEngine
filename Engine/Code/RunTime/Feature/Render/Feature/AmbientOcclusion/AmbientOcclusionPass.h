#pragma once

#include <Base.h>

namespace Spark::Render
{
    class PassContext;

    //! Screen-space ambient occlusion for the main view (SceneTextures::AmbientOcclusion), as
    //! XeGTAO's three compute passes: a view-space depth chain from SceneDepth, the visibility
    //! integration against it and GBufferNormal, and an edge-aware denoise. They run only
    //! while the view has a ViewAmbientOcclusion.
    struct AmbientOcclusionPass
    {
        static void SetUp(PassContext& ctx);
    };
}
