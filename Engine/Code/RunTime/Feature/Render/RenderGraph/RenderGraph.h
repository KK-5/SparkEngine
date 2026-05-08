#pragma once

#include <RHI/Command/CommandQueueContext.h>

#include <Pass/RHIContext.h>
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

    class RenderGraph
    {
    public:
        bool Init(RHI::Device& device, RHI::SwapChain& swapChain);

        void Shutdown();

        void ExecutePipeline(Pipeline& pipeline, uint32_t frameIndex);


    private:
        //! Walk per-frame imported resources (ImagePerFrame / BufferPerFrame on
        //! resource entities, ImageViewPerFrame / BufferViewPerFrame on view
        //! entities) and refresh BackingImage / BackingBuffer / BackingImageView /
        //! BackingBufferView from m_xxx[frameIndex]. Single-frame imports are
        //! handled by the builder's lazy-add path on first ImportImageAttachment;
        //! this function only touches per-frame variants — work is bounded by
        //! the number of per-frame resources (typically very small).
        void RefreshPerFrameBackings(RHIContext& context, uint32_t frameIndex);


        Ptr<RHI::Device>          m_device;
        Ptr<RHI::TransientResourcePool> m_pool;
        RHI::CommandQueueContext  m_commandQueueContext;
        RHI::FenceSet             m_crossQueueFences;
        RHIHandle                 m_swapchainResource;
        RHIHandle                 m_swapchainView;

        RenderGraphBuilder  m_builder;
        RenderGraphCompiler m_compiler;
        RenderGraphExecuter m_executer;
    };
}