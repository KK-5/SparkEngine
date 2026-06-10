#pragma once

#include <Base.h>
#include <Pass/PassBuilder.h>

namespace Spark::Render
{
    class PassContext;

    struct CopyFrameBufferPass
    {
        static void SetUp(PassContext& ctx);
    };
}