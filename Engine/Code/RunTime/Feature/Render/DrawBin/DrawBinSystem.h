#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassContext.h>
#include <Pass/Component/PassDrawListComponents.h>

#include "DrawBin.h"

namespace Spark::Render
{
    //! An encoding pass's instance counts, indexed by key value.
    using KeyInstanceCounts = eastl::array<KeyInstanceCount, PipelineKeyCount>;

    //! Counts an encoded instance under each bit of drawMask.
    void CountKeyInstance(KeyInstanceCounts& counts, PipelineKey key, DrawMask drawMask);

    //! Writes counts onto the keys' entities, making one for a key counted the first time.
    void PublishKeyInstanceCounts(RHI::RHIContext& rhiCtx, const KeyInstanceCounts& counts);

    //! The ranges of pass's bins a view is drawn by, a draw each: in index order, an empty bin
    //! left out. In records and counts; what a record is, and so its size, is the draw's.
    void CollectBinRanges(const PassContext& passCtx, Pass pass, eastl::vector<DrawBinRange>& out);

    //! Keeps every pass's DrawBins from the keys the instance encoding published: assigns a
    //! new key or a new pass, lays out each bin's capacity and first record, and writes
    //! each pass's PassBinTable from the assignments.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, updated after InstanceBindingSystem
    //! (whose counts it lays out) and before the render graph is built.
    class DrawBinSystem
    {
    public:
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx);
        //! Also lets the keys go: they are this system's data, though the encoding makes them.
        void Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx);
    };
}
