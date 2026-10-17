#pragma once

namespace Spark::Render
{
    class PassContext;

    //! Sorts the instances each view sees into the DrawBins of one drawing pass (InstanceLists):
    //! a pass per pass that draws a list, reading the visible list InstanceCullingPass writes
    //! for it and the pass's bin table and layout DrawBinSystem keeps.
    //!
    //! Declared after InstanceCullingPass and before the passes that draw what it writes. Each
    //! pass creates its pair of buffers every frame, also in one where no view draws the list.
    struct InstanceBinningPass
    {
        static void SetUp(PassContext& ctx);
    };
}
