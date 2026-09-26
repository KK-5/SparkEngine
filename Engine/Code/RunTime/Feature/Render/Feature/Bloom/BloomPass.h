#pragma once

#include <Base.h>

namespace Spark::Render
{
    class PassContext;

    //! Builds the glow (PostProcess::BloomName) from the scene downsample chain, smallest level
    //! first: each Scope upsamples the glow so far and adds the chain level of its own size.
    //! Runs only while the main view has ViewBloom.
    struct BloomPass
    {
        static void SetUp(PassContext& ctx);
    };
}
