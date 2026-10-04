#pragma once

#include <Base.h>

namespace Spark::Render
{
    class PassContext;

    //! Screen-space reflections for the main view (SceneTextures::ScreenSpaceReflections): one
    //! compute pass that marches each pixel's mirror ray through the scene depth and the HZB,
    //! and takes the color of what it hits from last frame's TemporalAA. It runs only while
    //! SceneTextures::ScreenSpaceReflections::FindView finds a view.
    struct ScreenSpaceReflectionsPass
    {
        static void SetUp(PassContext& ctx);
    };
}
