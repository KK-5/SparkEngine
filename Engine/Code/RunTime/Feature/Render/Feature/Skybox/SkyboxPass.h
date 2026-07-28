#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Full-screen skybox pass: samples the baked environment cubemap and composites
    //! it into SceneColor. No geometry — the VS synthesizes a full-screen triangle from
    //! SV_VertexID (empty input layout), so the pass just declares the SceneColor write
    //! and submits whatever DrawItems are tagged for it — the shared full-screen-triangle
    //! Drawable (FullScreenTriangleTag) is routed here by DrawItemRouter. Registered after
    //! DepthPrePass (which creates + clears SceneColor) and before CopyFrameBufferPass.
    struct SkyboxPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
