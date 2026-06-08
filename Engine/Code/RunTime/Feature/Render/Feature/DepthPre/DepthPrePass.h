#pragma once

#include <Base.h>
#include <Math/Vector2.h>

#include <RHI/Pipeline/InputStreamLayout.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Format.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    struct DepthPrePass
    {
        static RenderPassConfig DefaultConfig();

        static void SetUp(PassContext& ctx, const RenderPassConfig& cfg);
    };
}
