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