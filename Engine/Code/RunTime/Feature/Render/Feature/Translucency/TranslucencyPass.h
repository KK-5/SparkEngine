#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Translucent surfaces, forward and blended over SceneColor once the sky is in, before TAA
    //! (UE's BeforeDOF). Draws the translucent list back to front, testing against SceneDepth
    //! without writing it. Unlit for now: base colour with the material's opacity.
    struct TranslucencyPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
