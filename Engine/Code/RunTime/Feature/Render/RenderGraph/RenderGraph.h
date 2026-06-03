#pragma once

#include <Math/Vector2.h>

#include <RHI/Command/CommandQueueContext.h>

#include <RHI/Context/RHIContext.h>
#include <Pass/PassContext.h>

#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphExecuter.h"

namespace Spark::RHI
{
    class Device;
    class SwapChain;
    class TransientResourcePool;
}


namespace Spark::Render
{
    class Pipeline;

    class RenderGraph final
    {
    public:
        bool Init(RHI::Device& device);

        //! Register the swap chain into the active RHIContext as imported entities.
        //! Must run after Init, and requires RHIExecuteContext to be pushed by the caller.
        bool ImportSwapChain(RHI::SwapChain& swapChain);

        void Shutdown();

        //! renderSize is the render-output resolution for this frame (see
        //! RenderGraphBuilder::GetRenderSize). The caller (driver) decides where
        //! it comes from — window size, editor viewport, etc. — and the graph
        //! threads it to Build callbacks via the builder.
        void ExecutePipeline(PassContext& passContext, uint32_t frameIndex, const Math::Vector2Int& renderSize);

        RHI::CommandQueue& GetCommandQueue(RHI::HardwareQueueClass queueClass)
        {
            return m_commandQueueContext.GetCommandQueue(queueClass);
        }

        RHIHandle GetSwapchainResource() const { return m_swapchainResource; }
        RHIHandle GetSwapchainView() const     { return m_swapchainView; }

    private:
        //! Walk per-frame imported resources (ImagePerFrame / BufferPerFrame on
        //! resource entities, ImageViewPerFrame / BufferViewPerFrame on view
        //! entities) and refresh BackingImage / BackingBuffer / BackingImageView /
        //! BackingBufferView from m_xxx[frameIndex]. Single-frame imports are
        //! handled by the builder's lazy-add path on first ImportImageAttachment;
        //! this function only touches per-frame variants — work is bounded by
        //! the number of per-frame resources (typically very small).
        void RefreshPerFrameBackings(RHIContext& context, uint32_t frameIndex);

        //! Emit barriers transitioning every imported swap chain image to Present
        //! state on the graphics queue. Called at frame end, after the main
        //! segment execution loop and after each active queue's frame-end fence
        //! Signal (the producer queue's fence must be live before we queue.Wait
        //! on it for cross-queue producers).
        //!
        //! Supports any producer queue (Graphics direct render / Compute final
        //! composite / Copy queue blit): for non-Graphics producers, emits
        //! queue.Wait on the producer's frame-end fence and a cross-queue acquire
        //! barrier (srcQueue=producer, dstQueue=Graphics) on the transition cmd list.
        void SubmitSwapChainPresentTransition(RHIContext& ctx, RHI::Factory& factory);


        Ptr<RHI::Device>          m_device;
        Ptr<RHI::TransientResourcePool> m_pool;
        Ptr<RHI::PipelineLibrary>       m_pipelineLibrary;
        RHI::CommandQueueContext  m_commandQueueContext;
        RHI::FenceSet             m_crossQueueFences;
        RHIHandle                 m_swapchainResource;
        RHIHandle                 m_swapchainView;

        RenderGraphBuilder  m_builder;
        RenderGraphCompiler m_compiler;
        RenderGraphExecuter m_executer;
    };
}