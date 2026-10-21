#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassCapabilities.h>
#include <Pass/PassContext.h>
#include <Pass/Component/PassDrawListComponents.h>

#include "DrawBin.h"
#include "DrawContext.h"
#include "DrawGroup.h"

namespace Spark::Render
{
    //! A pass's DrawGroups, as few as a pass of a GPU-culled list has.
    using DrawGroupList = eastl::fixed_vector<const DrawGroup*, 8>;

    //! The DrawGroups of pass under view, in their index order. Its DrawGroups sorted, as Update
    //! leaves them.
    void CollectDrawGroups(const DrawContext& drawCtx, Pass pass, RHI::RHIHandle view, DrawGroupList& out);

    //! Turns what is known of each draw list into the DrawGroups of each pass drawing it: groups
    //! a pass's DrawListKeys by their variant in the pass, writes the pass's PassIndirectLayout
    //! from them, and the groups under each view the pass renders.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, updated after InstanceBindingSystem
    //! (whose DrawListKeys it reads) and before the render graph is built.
    class DrawBinSystem
    {
    public:
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx);
        void Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx);

    private:
        void UpdateGpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass);
        //! The table and layout of the pass's InstanceBinning, from its entries and groups.
        void WriteIndirectLayout(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass);

        // Reused from pass to pass and frame to frame.
        eastl::vector<DrawEntry> m_entries;
        eastl::vector<DrawGroup> m_groups;
        ViewHandleList           m_views;

        //! The image of each pass's PassIndirectLayout table, which the table's buffer is copied
        //! from at the next frame's start, so it stays put until then.
        eastl::hash_map<Pass, eastl::vector<IndirectLayoutEntry>> m_tables;
    };
}
