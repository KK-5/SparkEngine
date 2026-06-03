#include "RenderSystem.h"

#include <Log/ILogSystem.h>

#include <RHI/SwapChain/SwapChainDescriptor.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Pipeline/RenderTargetLayout.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Command/CommandList.h>

#include <Pass/Pass.h>
#include <Pass/PassTag.h>
#include <Pass/PassContext.h>
#include <Pass/PassBuilder.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

namespace Spark::Render
{
    bool RenderSystem::InitRHIData()
    {
        auto rhi = Service<RHI::RHIInterface>::Get();
        if (!rhi)
        {
            LOG_ERROR("[RenderSystem] There is no RHI service.");
            return false;
        }

        RHI::Factory* factory = rhi->GetRHIFactory();
        if (!factory)
        {
            LOG_ERROR("[RenderSystem] Get RHI factory failed.");
            return false;
        }

        // Device is initialized by the caller (main) via rhi->InitDevice() before
        // RenderSystem::Init runs. RenderSystem is a pure consumer here.
        RHI::Device* device = rhi->GetDevice();
        if (!device)
        {
            LOG_ERROR("[RenderSystem] RHI device is not initialized — call "
                      "RHIInterface::InitDevice() before RenderSystem::Init().");
            return false;
        }

        if (!m_renderGraph.Init(*device))
        {
            LOG_ERROR("[RenderSystem] RenderGraph init failed.");
            return false;
        }

        auto window = Service<Window::IWindowSystem>::Get();
        if (!window)
        {
            LOG_ERROR("[RenderSystem] There is no window service.");
            return false;
        }

        m_swapChain = factory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = device->GetDescriptor().m_frameCountMax;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = window->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.y;
        desc.m_dimensions.m_imageWidth = windowSize.x;
        desc.m_window = window->GetNativeHandle();
        RHI::ResultCode result = m_swapChain->Init(
            *device,
            m_renderGraph.GetCommandQueue(RHI::HardwareQueueClass::Graphics),
            desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Create swap chain failed!");
            return false;
        }

        return true;
    }

    bool RenderSystem::InitRenderUI()
    {
        RHI::ImGuiDescriptor desc;
        desc.m_rtvFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.m_dsvFormat = RHI::Format::D32_FLOAT;
        auto* device = Service<RHI::RHIInterface>::Get()->GetDevice();
        m_rednerUI.Bind(*device, m_renderGraph.GetCommandQueue(RHI::HardwareQueueClass::Graphics), desc);
        return true;
    }

    void RenderSystem::BuildPipeline()
    {
        auto& passContext = m_pipeline.GetPassContext();

        SPARK_RENDER_PASS(passContext, "UIPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .CustomPipeline()
            .Build([this](RenderGraphBuilder& builder)
            {
                ImportedImageAttachmentBindInfo bind;
                bind.m_slot   = RHI::InputName("ColorOutput");
                bind.m_view   = m_renderGraph.GetSwapchainView();
                bind.m_access = RHI::AttachmentAccess::Write;
                bind.m_usage  = RHI::AttachmentUsage::RenderTarget;
                bind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(1.f, 0.f, 0.f, 1.f);
                bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
                bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

                builder.ImportImageAttachment<SPARK_PASS_TAG("UIPass")>(
                    RHI::AttachmentId("SwapChain"), bind);
            })
            .Execute([this](ExecuteWork& work, RenderGraphExecuter&)
            {
                m_rednerUI.Render(work.m_commandList);
            })
            .Finalize();
    }

    void RenderSystem::InitPipeline()
    {
        // TODO: pipeline state object creation per pass — depends on shader asset wiring
        // (see commented block in git history). Left as no-op until shader pipeline is rebuilt.
    }

    void RenderSystem::InitInternal()
    {
        InitRHIData();

        // ImportSwapChain materializes swap chain entities into the active RHIContext;
        // the context is owned and pushed by the RHI layer (see RHIInterface),
        // so by this point RHIExecuteContext::Current() is already valid.
        m_renderGraph.ImportSwapChain(*m_swapChain);

        InitRenderUI();
        BuildPipeline();
        InitPipeline();

        PassExecuteContext::Push(m_pipeline.GetPassContext());

        TickBus::Handler::BusConnect();
    }

    void RenderSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
        PassExecuteContext::Pop();

        // RenderGraph::Shutdown drains the GPU and destroys the swap chain
        // entities it imported into RHIContext. After that the swap chain
        // itself can be released — any in-flight Present has completed and
        // the imported ImageView entities are gone.
        m_renderGraph.Shutdown();
    }

    void RenderSystem::OnTick(float deltaTime)
    {
        auto& passContext = *PassExecuteContext::Current(); 
        
        const uint32_t frameIndex =m_swapChain->GetCurrentImageIndex();

        // Driver decides the render-output resolution and hands it to the graph,
        // which threads it to Build callbacks via the builder. Today that's the
        // window size; an editor would feed its viewport panel size here instead.
        const Math::Vector2Int renderSize = Service<Window::IWindowSystem>::Get()->GetWindowSize();

        m_uiProcessFeature.Process();
        m_renderGraph.ExecutePipeline(passContext, frameIndex, renderSize);
        m_swapChain->Present();
    }
}
