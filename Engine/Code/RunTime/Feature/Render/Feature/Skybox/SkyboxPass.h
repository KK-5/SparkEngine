#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    //! Full-screen skybox pass: samples the baked environment cubemap and composites
    //! it into SceneColor. No geometry — the VS synthesizes a full-screen triangle from
    //! SV_VertexID (empty input layout), so its Scope draws DrawLinear(3, 0), and only once
    //! the sky cube is ready.
    struct SkyboxPass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
