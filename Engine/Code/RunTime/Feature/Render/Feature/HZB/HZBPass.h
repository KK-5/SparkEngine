#pragma once

#include <Base.h>

namespace Spark::Render
{
    class PassContext;

    //! Builds the hierarchical Z-buffer from the scene depth (SceneTextures::HZB): the closest
    //! and the furthest chain together, one Scope per mip, each reading the mip above it.
    struct HZBPass
    {
        static void SetUp(PassContext& ctx);
    };
}
