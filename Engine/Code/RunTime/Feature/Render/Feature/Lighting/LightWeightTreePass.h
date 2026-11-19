#pragma once

namespace Spark::Render
{
    class PassContext;

    //! Builds the per-frame LightWeightTree buffer.
    struct LightWeightTreePass
    {
        static void SetUp(PassContext& ctx);
    };
}
