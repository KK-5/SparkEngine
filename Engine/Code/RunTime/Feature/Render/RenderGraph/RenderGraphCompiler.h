#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <Log/ILogSystem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::RHI
{
    class Device;
    class TransientResourcePool;
    class ImageView;
    class BufferView;
    class PipelineLibrary;
}

namespace Spark::Render
{

    using QueueBasedPasses = eastl::array<eastl::vector<Pass>, static_cast<size_t>(RHI::HardwareQueueClass::Count)>;

    // Per-queue pre-frame barriers for static resources (StaticImportTag).
    // Compiled once before the per-pass compile loop, executed before any pass
    // work on each queue. Empty after the first frame when resources reach steady state.
    struct StaticPreBarriers
    {
        eastl::vector<RHI::PendingSync>  m_fenceWaits;
        eastl::vector<RHI::ImageBarrier> m_imageBarriers;
        eastl::vector<RHI::BufferBarrier> m_bufferBarriers;

        bool IsEmpty() const { return m_fenceWaits.empty() && m_imageBarriers.empty() && m_bufferBarriers.empty(); }
    };

    using StaticPreBarrierTable = eastl::array<StaticPreBarriers, static_cast<size_t>(RHI::HardwareQueueClass::Count)>;

    class RenderGraphCompiler
    {
    public:
        uint32_t GetFrameIndex() const { return m_frameIndex; }

    private:
        friend class RenderGraph;

        void Begin(uint32_t frameIndex);

        void End();

        QueueBasedPasses CompilePassCrossQueue(eastl::span<Pass> passes);

        // Successor-driven variant: signal emitted when processing the source pass.
        QueueBasedPasses CompilePassCrossQueue2(eastl::span<Pass> passes);

        //! Allocate transient images/buffers from the pool, materialize their
        //! views, and write the backing pointers / view handles back onto the
        //! resource and attachment entities in RHIContext. Caller must ensure
        //! the pool's batch is open (post-OnFrameBegin). On return, the pool is
        //! sealed and ready for GetDeviceMemoryBarriers queries during per-pass
        //! barrier compilation.
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
        //! the pass entity. Reads each attachment's view via the BackingImageView
        //! component on the view entity — transient and imported, single-frame
        //! and per-frame views are all unified through this borrowed pointer.
        //! Caller must have run the per-pass attachment tagging step first, and
        //! CompileTransientResources must already have materialized transient views.
        void CompileRenderPassBeginInfo(Pass pass, PassContext& passContext, RHIContext& context);

        //! Compile per-queue pre-frame fence-waits + acquire-barriers for all
        //! StaticImportTag attachments. Called once before the per-pass compile
        //! loop. Reads RHI resource state directly — after the first frame the
        //! resource is in its steady state and the resulting barrier lists are empty.
        StaticPreBarrierTable CompileStaticResourceBarriers(RHIContext& context);

        //! Compile PSO for each non-custom pipeline pass and cache the result
        //! as PassCompiledPSO on the pass entity. Skips passes that already have
        //! a cached PSO (no PassPSODirtyTag).
        void CompilePipelineStates(
            PassContext&          passContext,
            RHI::Device&          device,
            RHI::PipelineLibrary* pipelineLibrary);


        //! Sweeps every entity carrying ShaderBindingsUpdateTag + Components::ShaderBindings,
        //! dispatches Compile on each, and clears the tag. User code (typically
        //! CreatePassShaderBindings + MarkShaderBindingsUpdate) drives the dirty bit.
        void CompileShaderInputs(RHI::Device& device, RHIContext& context);


        void CompileDrawRequests(RHI::Device& device, RHIContext& context, RHI::PipelineLibrary* pipelineLibrary);

        // Per-queue monotonically increasing counter for cross-queue fence values.
        // Incremented each time a queue emits a signal; never resets across frames.
        eastl::array<uint64_t, RHI::HardwareQueueClassCount> m_crossQueueFenceValues{1, 1, 1};

        uint32_t m_frameIndex { 0 };
    };
}