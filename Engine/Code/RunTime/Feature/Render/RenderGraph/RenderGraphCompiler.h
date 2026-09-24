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
    class ImagePool;
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

        // Successor-driven variant: signal emitted when processing the source pass.
        QueueBasedPasses CompilePassCrossQueue2(eastl::span<Pass> passes);

        //! Allocate transient images/buffers from the pool, materialize their
        //! views, and write the backing pointers / view handles back onto the
        //! resource and attachment entities in RHIContext. Caller must ensure
        //! the pool's batch is open (post-OnFrameBegin). On return, the pool is
        //! sealed and ready for GetDeviceMemoryBarriers queries during per-pass
        //! barrier compilation.
        void CompileTransientResources(RHI::TransientResourcePool& pool);

        //! Back every ExtractedImage resource with a pooled image from `pool`, which
        //! outlives the frame. Runs after CompileTransientResources, which links their
        //! attachments and leaves them out of the transient pool.
        void CompileExtractedImages(RHI::ImagePool& pool);

        //! Put the Scope and ScopeAttachment storages in stream order: Scopes by (pass topo
        //! position, index in pass); attachments by (their Scope, resource), so each Scope's
        //! attachments are contiguous and those of one resource adjacent. Needs every attachment
        //! linked to its resource, so runs after CompileTransientResources /
        //! CompileExtractedImages. Nothing may add or remove either component afterwards.
        void SortScopes(PassContext& passContext, RHIContext& context);

        //! Walk the sorted attachments once, Scope by Scope, and put on them the barriers their
        //! accesses need: Pre*Barrier on the first attachment of each (Scope, resource) group,
        //! Post*Barrier (cross-queue release) on the producer's attachment, PreAliasingBarrier on
        //! the one first touching a transient resource that `pool` placed over another's memory.
        //! A barrier is needed when the state differs or either side writes; dropping same-state
        //! ones is the backend's call. Runs after SortScopes.
        void CompileScopeBarriers(
            PassContext& passContext, RHIContext& context, const RHI::TransientResourcePool& pool);

        //! Turn the cross-queue waits CompileScopeBarriers recorded into fence values: walks
        //! Scopes in stream order, gives each ScopeSignal its queue's next value, and resolves
        //! each ScopeWait to its producers' values, dropping any an earlier wait already covers.
        void CompileScopeSync(PassContext& passContext, RHIContext& context);

        //! Transitional, until the executer walks Scopes: copies Scope waits / signals and the
        //! attachments' external waits onto the passes, where BuildSegments still reads them.
        void CollectPassSync(PassContext& passContext, RHIContext& context);

        //! Transitional: the per-queue pass lists CompilePassCrossQueue2 used to return.
        QueueBasedPasses SplitPassesByQueue(eastl::span<const Pass> passes, const PassContext& passContext);

        //! Build each render pass Scope's RHI::RenderPassBeginInfo from its attachments and put it
        //! on the Scope. Colors go by ColorAttachmentIndex, not storage order. Runs after
        //! SortScopes.
        void CompileScopeBeginInfo(PassContext& passContext, RHIContext& context);

        //! Transitional, until the executer walks Scopes: copies each Scope's BeginInfo onto its
        //! pass, where the executer still reads it.
        void CollectPassBeginInfo(PassContext& passContext, RHIContext& context);

        //! Transitional, until the executer walks Scopes: gathers the barriers on each pass's
        //! attachments, plus its transient aliasing barriers, into the PassBarriers the
        //! executer still reads.
        void CollectPassBarriers(
            eastl::span<const Pass>     passes,
            PassContext&                passContext,
            RHIContext&                 context,
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
        //! AttachmentCompilingTag) into a RHI::RenderPassBeginInfo component on the
        //! pass entity. Each attachment's view is resolved from its resource's view
        //! cache (single-frame ImageViewCache or per-frame ImageViewCachePerFrame)
        //! keyed by the attachment's view descriptor. Caller must have run the
        //! per-pass attachment tagging step first, and CompileTransientResources
        //! must already have materialized the transient resources.
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


        //! Resolves each pass's PassSharedBindings: its own space2 group plus every tag it
        //! declared via .Binds<>. Must run after the per-pass compile hooks — sampling
        //! passes create their SRG there, and resolving earlier would miss it for a frame.
        void CompilePassSharedBindings(PassContext& passContext, RHIContext& context);

        //! Put on each Scope the submit state its pass decides (ScopeState): the compiled PSO
        //! and, when there is one, the pass's shared bindings. Runs after CompilePipelineStates
        //! and CompilePassSharedBindings.
        void CompileScopeState(PassContext& passContext, RHIContext& context);

        //! Lay each Scope's submissions out in submitList and record its ScopeSubmitRange: per
        //! ready view, the view's handle then the items the pass collects for it; items only for
        //! a pass that renders no view. A render pass whose BeginInfo gives no target extent
        //! submits nothing. Runs after CompileScopeBeginInfo.
        void CompileScopeSubmitRanges(
            PassContext& passContext, RHIContext& context, eastl::vector<RHIHandle>& submitList);

        //! Sweeps every entity carrying ShaderBindingsUpdateTag + Components::ShaderBindings,
        //! dispatches Compile on each, and clears the tag. User code (typically
        //! CreatePassShaderBindings + MarkShaderBindingsUpdate) drives the dirty bit.
        void CompileShaderInputs(RHI::Device& device, RHIContext& context);

        // Per-queue monotonically increasing counter for cross-queue fence values.
        // Incremented each time a queue emits a signal; never resets across frames.
        eastl::array<uint64_t, RHI::HardwareQueueClassCount> m_crossQueueFenceValues{1, 1, 1};

        uint32_t m_frameIndex { 0 };

        eastl::vector<RHI::DeviceMemoryBarrier> m_aliasingScratch;

        static constexpr bool s_scopeOrderValidation { true };
    };
}