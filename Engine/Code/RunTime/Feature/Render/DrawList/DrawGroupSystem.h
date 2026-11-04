#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassCapabilities.h>
#include <Pass/PassContext.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <Pass/Component/PassIndirectLayout.h>
#include <Staging/StagingBuffer.h>

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
        bool Init(RHI::Device& device);
        void Update(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx);
        void Shutdown(PassContext& passCtx);

    private:
        void UpdateGpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass);
        //! Groups cut out of each view's draws, which DrawListCullingSystem wrote in the list's order.
        void UpdateCpuCulledPass(RHI::RHIContext& rhiCtx, PassContext& passCtx, DrawContext& drawCtx, Pass pass);
        void CollectPassViews(RHI::RHIContext& rhiCtx, PassContext& passCtx, Pass pass);
        //! Sizes every GPU-culled pass's table and prepares their shared buffer for this frame.
        bool PrepareIndirectLayoutTables(PassContext& passCtx, DrawContext& drawCtx);
        //! The table and layout of the pass's InstanceBinning, from its entries and groups.
        void WriteIndirectLayout(PassContext& passCtx, Pass pass);

        // Reused from pass to pass and frame to frame.
        eastl::vector<DrawEntry> m_entries;
        eastl::vector<DrawGroup> m_groups;
        ViewHandleList           m_views;

        //! All passes' tables share one host buffer per frame, each referring to its own range.
        StagingBuffer m_tables;
        //! Required by both the structured view and its bindless raw view.
        uint32_t m_tableAlignment = sizeof(IndirectLayoutEntry);
    };
}
