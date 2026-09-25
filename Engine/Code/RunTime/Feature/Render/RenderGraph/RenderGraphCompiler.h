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
    class FenceSet;
    class TransientResourcePool;
    class ImagePool;
    class ImageView;
    class BufferView;
    class PipelineLibrary;
}

namespace Spark::Render
{

    // Per-queue pre-frame barriers for static resources (StaticImportTag).
    // Compiled once before the per-pass compile hooks, executed before any Scope
    // on each queue. Empty after the first frame when resources reach steady state.
    struct StaticPreBarriers
    {
        eastl::vector<RHI::PendingSync>  m_fenceWaits;
        eastl::vector<RHI::ImageBarrier> m_imageBarriers;
        eastl::vector<RHI::BufferBarrier> m_bufferBarriers;
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

        //! Allocate transient images/buffers from the pool, materialize their
        //! views, and write the backing pointers / view handles back onto the
        //! resource and attachment entities in RHIContext. Caller must ensure
        //! the pool's batch is open (post-OnFrameBegin). On return, the pool is
        //! sealed and ready for GetAliasingBarrier queries during Scope
        //! barrier compilation.
        void CompileTransientResources(RHI::TransientResourcePool& pool);

        //! Back every ExtractedImage resource with a pooled image from `pool`, which
        //! outlives the frame. Runs after CompileTransientResources, which links their
        //! attachments and leaves them out of the transient pool.
        void CompileExtractedImages(RHI::ImagePool& pool);

        //! Put the Scope, ScopeAttachment and ScopeItem storages in stream order: Scopes by (pass
        //! topo position, index in pass); attachments by (their Scope, resource), so each Scope's
        //! attachments are contiguous and those of one resource adjacent; items by their Scope.
        //! Each Scope records where its runs are in ScopeAttachmentRange / ScopeItemRange. Needs
        //! every attachment linked to its resource, so runs after CompileTransientResources /
        //! CompileExtractedImages. Nothing may add or remove any of these components afterwards.
        void SortScopes(PassContext& passContext, RHIContext& context);

        //! The queues this frame's Scopes run on, into m_activeQueues: the executer opens each
        //! of them before the first Scope, and the frame end stamps only those.
        void CompileActiveQueues(RHIContext& context);

        bool IsQueueActive(RHI::HardwareQueueClass queue) const
        {
            return CheckBitsAny(m_activeQueues, RHI::GetHardwareQueueClassMask(queue));
        }

        //! Walk the sorted attachments once, Scope by Scope, and put on them the barriers their
        //! accesses need: Pre*Barrier on the first attachment of each (Scope, resource) group,
        //! Post*Barrier (cross-queue release) on the producer's attachment, PreAliasingBarrier on
        //! the one first touching a transient resource that `pool` placed over another's memory.
        //! A barrier is needed when the state differs or either side writes; dropping same-state
        //! ones is the backend's call. Runs after SortScopes.
        void CompileScopeBarriers(
            PassContext& passContext, RHIContext& context, const RHI::TransientResourcePool& pool);

        //! Turn the cross-queue waits CompileScopeBarriers recorded into fences and values: walks
        //! Scopes in stream order, gives each ScopeSignal its queue's fence in crossQueueFences
        //! and next value, and resolves each ScopeWait to its producers' signals, dropping any an
        //! earlier wait already covers.
        void CompileScopeSync(PassContext& passContext, RHIContext& context, RHI::FenceSet& crossQueueFences);

        //! Build each render pass Scope's RHI::RenderPassBeginInfo from its attachments and put it
        //! on the Scope. Colors go by ColorAttachmentIndex, not storage order. Runs after
        //! SortScopes.
        void CompileScopeBeginInfo(PassContext& passContext, RHIContext& context);

        //! Compile per-queue pre-frame fence-waits + acquire-barriers for all
        //! StaticImportTag attachments. Called once before the per-pass compile
        //! hooks. Reads RHI resource state directly — after the first frame the
        //! resource is in its steady state and the resulting barrier lists are empty.
        StaticPreBarrierTable CompileStaticResourceBarriers(RHIContext& context);

        //! Compile PSO for each non-custom pipeline pass and cache the result
        //! as PassCompiledPSO on the pass entity. Skips passes that already have
        //! a cached PSO (no PassPSODirtyTag).
        void CompilePipelineStates(
            PassContext&          passContext,
            RHI::Device&          device,
            RHI::PipelineLibrary* pipelineLibrary);

        //! Write what Scopes declared for their pass's per-pass space into its bindings: the views
        //! of attachments bound to inputs (ShaderInputBinding), samplers and constants. A pass
        //! that declared any of these gets null in the image and buffer inputs none of them
        //! bound. Scopes of one pass share its per-pass space: an input two of them set must be
        //! the same, which is the pass's to keep, not checked here. Runs after SortScopes
        //! and the transient / extracted stages (views need backing), before CompileShaderInputs.
        void CompileScopeBindings(PassContext& passContext, RHIContext& context);

        //! Put on each Scope the submit state its pass decides (ScopeState): the compiled PSO
        //! and, when there is one, the bindings bound once per Scope — the pass's own
        //! (PassBindings) then the shared ones it declared via .Binds<>. Runs after
        //! CompilePipelineStates.
        void CompileScopeState(PassContext& passContext, RHIContext& context);

        //! Lay each Scope's submissions out in submitList and record its ScopeSubmitRange: per
        //! ready view, the view's handle then the Scope's own items and those the pass collects
        //! for it through a static .Accepts; items only for
        //! a pass that renders no view. A render pass whose BeginInfo gives no target extent
        //! submits nothing. Runs after CompileScopeBeginInfo.
        void CompileScopeSubmitRanges(
            PassContext& passContext, RHIContext& context, eastl::vector<RHIHandle>& submitList);

        //! Sweeps every entity carrying ShaderBindingsUpdateTag + Components::ShaderBindings,
        //! dispatches Compile on each, and clears the tag. User code (typically
        //! SetShader* helpers or MarkShaderBindingsUpdate) drives the dirty bit.
        void CompileShaderInputs(RHI::Device& device, RHIContext& context);

        // Per-queue monotonically increasing counter for cross-queue fence values.
        // Incremented each time a queue emits a signal; never resets across frames.
        eastl::array<uint64_t, RHI::HardwareQueueClassCount> m_crossQueueFenceValues{1, 1, 1};

        uint32_t m_frameIndex { 0 };

        RHI::HardwareQueueClassMask m_activeQueues { RHI::HardwareQueueClassMask::None };

        static constexpr bool s_scopeOrderValidation { true };
    };
}