#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassContext.h>
#include <Pass/Component/PassDrawListComponents.h>

#include "DrawBin.h"
#include "DrawContext.h"
#include "DrawGroup.h"

namespace Spark::Render
{
    //! A pass's DrawBins, as few as a pass of a GPU-culled list has.
    using DrawBinList = eastl::fixed_vector<const DrawBin*, 8>;

    //! The DrawBins of pass under view, in their index order. Its DrawBins sorted, as Update
    //! leaves them.
    void CollectDrawBins(const DrawContext& drawCtx, Pass pass, RHI::RHIHandle view, DrawBinList& out);

    //! Turns what is known of each draw list into the DrawBins of each pass drawing it: groups
    //! a pass's DrawListKeys by their variant in the pass, and writes the pass's bin table and
    //! PassBinLayout from the groups.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, updated after InstanceBindingSystem
    //! (whose DrawListKeys it reads) and before the render graph is built.
    class DrawBinSystem
    {
    public:
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx);
        void Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx);

    private:
        void UpdatePass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass);

        // Reused from pass to pass and frame to frame.
        eastl::vector<DrawEntry> m_entries;
        eastl::vector<DrawGroup> m_groups;

        //! The image of each pass's bin table, which the table's buffer is copied from at the
        //! next frame's start, so it stays put until then.
        eastl::hash_map<Pass, eastl::vector<DrawBinTableEntry>> m_binTables;
    };
}
