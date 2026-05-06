#include "RenderGraph.h"

#include <Log/SpdLogSystem.h>

#include <Core/Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>

#include <Pass/Pipeline.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
    bool RenderGraph::Init(RHI::Device& device, RHI::SwapChain& swapChain)
    {
        RHI::ResultCode result = m_commandQueueContext.Init(device);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] CommandQueueContext initialize failed.");
            return false;
        }

        m_crossQueueFences.Init(device, RHI::FenceState::Reset);

        const uint32_t imageCount = swapChain.GetImageCount();
        auto& context = *RHIExecuteContext::Current();
        auto* factoryPtr = Service<RHI::Factory>::Get();
        ASSERT(factoryPtr != nullptr, "RHI::Factory service is not registered.");
        auto& factory = *factoryPtr;

        m_swapchainResource = context.CreateEntity();
        SwapChainImages swapChainImages;
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            swapChainImages.images[i] = swapChain.GetImage(i);
        }
        context.Add<SwapChainImages>(m_swapchainResource, eastl::move(swapChainImages));
        context.Add<ImportedTag>(m_swapchainResource);
        context.Add<ResourceName>(m_swapchainResource, ObjectName{"SwapChain"});
        context.Add<ImportedResourceState>(
            m_swapchainResource,
            RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown},
            RHI::ResourceState{RHI::AttachmentUsage::Present, RHI::AttachmentAccess::Read}
        );

        m_swapchainView = context.CreateEntity();
        SwapChainViews swapChainView;
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            auto image = swapChain.GetImage(i);
            Ptr<RHI::ImageView> imageview = factory.CreateImageView();
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin = 0;
            viewDesc.m_mipSliceMax = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            RHI::ResultCode viewResult = imageview->Init(*image, viewDesc);
            if (viewResult != RHI::ResultCode::Success)
            {
                LOG_ERROR("Create swap chain view failed.");
                continue;
            }
            swapChainView.imageViews[i] = eastl::move(imageview);
        }
        context.Add<SwapChainViews>(m_swapchainView, eastl::move(swapChainView));
        context.Add<ResourceName>(m_swapchainView, ObjectName{"SwapChainView"});
        context.Add<ViewHierarchy>(
            m_swapchainView,
            m_swapchainResource,
            NullHandle,
            NullHandle
        );

        context.Add<ResourceHierarchy>(
            m_swapchainResource,
            m_swapchainView
        );

        RHI::TransientResourcePoolDescriptor desc;
        // Use default config
        result = m_pool->Init(device, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] TransientResourcePool initialize failed.");
            return false;
        }

        return true;
    }

    void RenderGraph::ExecutePipeline(Pipeline& pipeline, uint32_t frameIndex)
    {
        auto& passContext = pipeline.GetPassContext();
        PassExecuteContext::Push(passContext);
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);
        m_commandQueueContext.Begin();

        // Push swap chain image and imageview
        auto& context = *RHIExecuteContext::Current();
        auto& images = context.Get<SwapChainImages>(m_swapchainResource);
        auto& views  = context.Get<SwapChainViews>(m_swapchainView);
        context.AddOrReplace<BackingImage>(m_swapchainResource, BackingImage{ images.images[frameIndex] });
        context.AddOrReplace<ImageViewPtr>(m_swapchainView, ImageViewPtr{ views.imageViews[frameIndex] });

        auto passFuncs = passContext.GetView<PassFunctions, ActivePassTag>();

        ////////////////////////////////////////////////
        // Build
        m_builder.Begin();

        passFuncs.each([&](Pass pass, PassFunctions& funcs)
        {
            m_builder.BeginPass(pass);
            if (funcs.m_buildFunction)
            {
                funcs.m_buildFunction(m_builder);
            }
            m_builder.EndPass();
        });

        eastl::vector<Pass> passes = m_builder.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Compile
        QueueBasedPasses queueBasedPasses = m_compiler.CompilePassCrossQueue2(passes);

        m_compiler.CompileTransientResources(passes, *m_pool);

        m_pool->Seal();

        for (auto pass : passes)
        {
            ASSERT(passContext.Has<PassAttachmentMarker>(pass),
                "The Pass {} has not PassAttachmentMarker", passContext.Get<PassName>(pass).m_name.GetCStr());
            passContext.Get<PassAttachmentMarker>(pass).m_markFn(context);

            auto& func = passContext.Get<PassFunctions>(pass);
            if (func.m_compileFunction)
            {
                func.m_compileFunction(m_compiler);
            }

            m_compiler.CompileResourceBarriers(pass, passContext, context, *m_pool);

            if (passContext.Has<RenderPassTag>(pass))
            {
                m_compiler.CompileRenderPassBeginInfo(pass, passContext, context);
            }

            context.Clear<AttachmentCompilingTag>();
        }
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Execute

        m_executer.BuildExecuteTable(queueBasedPasses, passContext);

        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "[RenderGraph] RHI::Factory service not registered.");

        auto& queueSegments = m_executer.GetQueueSegments();

        for (uint32_t qi = 0; qi < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++qi)
        {
            auto& segments = queueSegments[qi];
            if (segments.empty())
            {
                continue;
            }

            const auto queueClass = static_cast<RHI::HardwareQueueClass>(qi);
            auto& queue = m_commandQueueContext.GetCommandQueue(queueClass);
            auto& device = queue.GetDevice();

            for (auto& segment : segments)
            {
                for (const auto& wait : segment.m_waits)
                {
                    queue.Wait(m_crossQueueFences.GetFence(wait.m_queue), wait.m_value);
                }

                for (auto& group : segment.m_groups)
                {
                    eastl::vector<RHI::CommandList*> cmdLists;

                    for (auto& work : group.m_works)
                    {
                        m_executer.Execute(work, *factory, device, queueClass, passContext);
                        cmdLists.push_back(work.m_commandList);
                    }

                    queue.ExecuteCommands(cmdLists);
                }

                if (segment.m_signal)
                {
                    queue.Signal(m_crossQueueFences.GetFence(segment.m_signal->m_queue), segment.m_signal->m_value);
                }
            }
        }

        ////////////////////////////////////////////////



        m_commandQueueContext.End();
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
        PassExecuteContext::Pop();
    }
}