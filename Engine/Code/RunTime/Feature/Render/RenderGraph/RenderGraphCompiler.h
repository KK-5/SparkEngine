#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <Log/ILogSystem.h>

#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <PipelineState/DrawPipelineCache.h>
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
        //! sealed, and each transient resource's state is where its first barrier starts.
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

        //! The queues this frame's Scopes run on, into m_activeQueues: the frame end signals
        //! and stamps only those.
        void CompileActiveQueues(RHIContext& context);

        bool IsQueueActive(RHI::HardwareQueueClass queue) const
        {
            return CheckBitsAny(m_activeQueues, RHI::GetHardwareQueueClassMask(queue));
        }

        //! Walk the Scopes in stream order and give the uploads awaiting their acquire to the
        //! first that uses a tag of their target (ScopeUses): a ScopeUpload on the request, its
        //! transfer taken to the tag's access. A Scope on another queue that uses one later waits
        //! for that Scope. Runs after SortScopes.
        void CompileScopeUploads(PassContext& passContext, RHIContext& context);

        //! Walk the sorted attachments once, Scope by Scope, and put on them the barriers their
        //! accesses need: Pre*Barrier on the first attachment of each (Scope, resource) group;
        //! for a resource coming from another queue, Pre*Acquire in its place and Post*Release
        //! on the producer's attachment; and a swap chain's Present
        //! transition (PostImageBarrier) on its last attachment, which must be on Graphics.
        //! An imported resource with uploads awaiting their acquire is acquired by the Scope that
        //! first accesses it. Last, each Scope's uploads get their ScopeUploadRange.
        //! A barrier is needed when the state differs or either side writes; dropping same-state
        //! ones is the backend's call. Runs after CompileScopeUploads.
        void CompileScopeBarriers(PassContext& passContext, RHIContext& context);

        //! Put an ExternalWait on the attachment by which each queue first touches an imported
        //! resource that another queue left pending (e.g. an upload on the Copy queue): its
        //! PendingSync, when the fence has not been reached. The queue it was left on is the
        //! one in the resource's own record; a buffer several queues share has none, and is
        //! waited for whichever queue left the fence. Runs after SortScopes.
        void CompileExternalWaits(RHIContext& context);

        //! Turn the cross-queue waits CompileScopeBarriers recorded into fences and values: walks
        //! Scopes in stream order, gives each ScopeSignal its queue's fence in crossQueueFences
        //! and next value, and resolves each ScopeWait to its producers' signals, dropping any an
        //! earlier wait already covers.
        void CompileScopeSync(PassContext& passContext, RHIContext& context, RHI::FenceSet& crossQueueFences);

        //! Build each render pass Scope's RHI::RenderPassBeginInfo from its attachments and put it
        //! on the Scope. Colors go by ColorAttachmentIndex, not storage order. Runs after
        //! SortScopes.
        void CompileScopeBeginInfo(PassContext& passContext, RHIContext& context);

        //! Compile PSO for each non-custom pipeline pass and cache the result
        //! as PassCompiledPSO on the pass entity. Skips passes that already have
        //! a cached PSO (no PassPSODirtyTag).
        void CompilePipelineStates(
            PassContext&          passContext,
            RHI::Device&          device,
            RHI::PipelineLibrary* pipelineLibrary);

        //! Write what Scopes declared for their pass's per-pass space into its bindings: the views
        //! of attachments bound to inputs (ShaderInputBinding), samplers and constants; and the
        //! uints attachments give constants (IndexBinding, PreviousFrameValidBinding), a root
        //! constant's into its Scope's block. A pass
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

        //! Give each item that reads its arguments from a buffer of the graph
        //! (ItemIndirectArguments) that buffer's backing, and a draw its count buffer's and how
        //! many records the argument buffer holds. Runs after the transient stage.
        void CompileItemIndirectArguments(PassContext& passContext, RHIContext& context);

        //! Bind into each draw that reads its vertices and indices from buffers of the graph
        //! (ItemInputAssembly) those buffers' backings, whole, each vertex stream with the stride
        //! its slot has in the pass's input layout. Runs after the transient stage.
        void CompileItemInputAssembly(PassContext& passContext, RHIContext& context);

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

        //! Every draw PSO: a pass's default (CompilePipelineStates) and each DrawGroup's, when its
        //! item is expanded. A stopgap until a real PSO cache exists; that one may belong in
        //! RenderGraph, which makes and manages the RHI objects, handed in where needed.
        DrawPipelineCache m_drawPipelineCache;

        // Per-queue monotonically increasing counter for cross-queue fence values.
        // Incremented each time a queue emits a signal; never resets across frames.
        eastl::array<uint64_t, RHI::HardwareQueueClassCount> m_crossQueueFenceValues{1, 1, 1};

        uint32_t m_frameIndex { 0 };

        RHI::HardwareQueueClassMask m_activeQueues { RHI::HardwareQueueClassMask::None };

        static constexpr bool s_scopeOrderValidation { true };

        //! Scopes of one pass that set one per-pass input must set it to one value.
        static constexpr bool s_scopeBindingValidation { true };
    };
}