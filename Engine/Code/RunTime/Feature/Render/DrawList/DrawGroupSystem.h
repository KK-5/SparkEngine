#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassCapabilities.h>
#include <Pass/PassContext.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <Pass/Component/PassIndirectLayout.h>

#include "DrawContext.h"
#include "DrawGroup.h"

namespace Spark::Render
{
    //! Turns what is known of each draw list into the DrawGroups of each pass drawing it, under
    //! each view the pass renders. A list the GPU culls: groups its DrawListKeys by their variant
    //! in the pass and writes the pass's PassIndirectLayout from them. A list the CPU culls: cuts
    //! each view's VisibleInstances, whose draws are already written, where the variant changes.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, updated after InstanceBindingSystem and
    //! DrawListCullingSystem (whose elements it reads) and before the render graph is built.
    class DrawGroupSystem
    {
    public:
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx);
        void Shutdown(RHI::RHIContext& rhiCtx, PassContext& passCtx);

    private:
        void UpdateGpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass);
        //! Groups cut out of each view's draws, which DrawListCullingSystem wrote in the list's order.
        void UpdateCpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass);
        void CollectPassViews(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass);
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
