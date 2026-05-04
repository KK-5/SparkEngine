#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <Log/SpdLogSystem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>

namespace Spark::RHI
{
    class TransientResourcePool;
}

namespace Spark::Render
{

    using QueueBasedPasses = eastl::array<eastl::vector<Pass>, static_cast<size_t>(RHI::HardwareQueueClass::Count)>;

    class RenderGraphCompiler
    {
    public:
        RHI::ShaderResource* GetShaderResource() const;

        RHI::ImageView*  GetImageView(RHI::InputName slot) const;

        RHI::BufferView* GetBufferView(RHI::InputName slot) const;

    private:
        friend class RenderGraph;

        QueueBasedPasses CompilePassCrossQueue(eastl::span<Pass> passes);

        //! Allocate transient images/buffers from the pool, materialize their
        //! views, and write the backing pointers / view handles back onto the
        //! resource and attachment entities in RHIContext. Caller must ensure
        //! the pool's batch is open (post-OnFrameBegin, pre-seal).
        void CompileTransientResources(
            eastl::span<Pass>           passes,
            RHI::TransientResourcePool& pool);


        //! Compile all barriers for a single pass. Must be called in topo-sort
        //! order so that cross-queue Release/Acquire pairs are written to the
        //! correct upstream passes. Emits aliasing barriers first (heap
        //! ownership transfer), then image barriers (including cross-queue
        //! Acquire for the current pass), then buffer barriers. Result is
        //! stored as PassBarriers on the pass entity in PassContext.
        void CompileResourceBarriers(
            Pass                        pass,
            PassContext&                passContext,
            RHIContext&                 context,
            RHI::TransientResourcePool& pool);

        //! Translate this pass's ImagePassAttachments (the ones tagged with
        //! AttachmentCompilingTag) into a RHI::RenderPassBeginInfo component on
        //! the pass entity. Reads each attachment's view via the ImageViewPtr
        //! component on the view entity (transient/imported are unified — the
        //! distinction is carried by tags on the view entity, not the component
        //! type). Caller must have run the per-pass attachment tagging step
        //! first, and CompileTransientResources must already have materialized
        //! transient views.
        void CompileRenderPassBeginInfo(Pass pass, PassContext& passContext, RHIContext& context);
    };
}