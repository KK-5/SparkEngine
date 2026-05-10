#include "RenderSystem.h"

#include <Log/SpdLogSystem.h>

#include <RHI/SwapChain/SwapChainDescriptor.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Pipeline/RenderTargetLayout.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Command/CommandList.h>

#include <Pass/Pass.h>
#include <Pass/PassTag.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>
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

        m_rhiData.m_factory = rhi->GetRHIFactory();
        if (!m_rhiData.m_factory)
        {
            LOG_ERROR("[RenderSystem] Get RHI factory failed.");
            return false;
        }

        RHI::PhysicalDeviceList devList = m_rhiData.m_factory->EnumeratePhysicalDevices();
        ASSERT(devList.size() > 0, "[RenderSystem] No physical devices available.");
        Ptr<RHI::PhysicalDevice> selectDevice = devList.front();

        RHI::DeviceDescriptor deviceDesc;
        deviceDesc.m_frameCountMax = 3;
        m_rhiData.m_device = m_rhiData.m_factory->CreateDevice();
        if (m_rhiData.m_device->Init(*selectDevice, deviceDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Init device failed.");
            return false;
        }

        if (!m_renderGraph.Init(*m_rhiData.m_device))
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

        m_rhiData.m_swapChain = m_rhiData.m_factory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = deviceDesc.m_frameCountMax;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = window->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.second;
        desc.m_dimensions.m_imageWidth = windowSize.first;
        desc.m_window = window->GetNativeHandle();
        RHI::ResultCode result = m_rhiData.m_swapChain->Init(
            *m_rhiData.m_device,
            m_renderGraph.GetCommandQueue(RHI::HardwareQueueClass::Graphics),
            desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Create swap chain failed!");
            return false;
        }

        m_rhiData.m_pipelineLibrary = m_rhiData.m_factory->CreatePipelineLibrary();
        RHI::PipelineLibraryDescriptor pipelineLibraryDesc;
        if (m_rhiData.m_pipelineLibrary->Init(*m_rhiData.m_device, pipelineLibraryDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create pipeline library failed!");
            return false;
        }

        return true;
    }

    bool RenderSystem::InitRenderUI()
    {
        RHI::ImGuiDescriptor desc;
        desc.m_rtvFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.m_dsvFormat = RHI::Format::D32_FLOAT;
        m_rednerUI.Bind(*m_rhiData.m_device, m_renderGraph.GetCommandQueue(RHI::HardwareQueueClass::Graphics), desc);
        return true;
    }

    void RenderSystem::BuildPipeline()
    {
        auto& passContext = m_pipeline.GetPassContext();

        Pass uiPass = passContext.CreateEntity();
        passContext.Add<PassName>(uiPass, ObjectName("UIPass"));
        passContext.Add<ActivePassTag>(uiPass);
        passContext.Add<RenderPassTag>(uiPass);
        passContext.Add<PassAttachmentMarker>(uiPass, MarkPassAttachmentCompiling<SPARK_PASS_TAG("UIPass")>());

        passContext.Add<PassExecuteQueue>(uiPass, PassExecuteQueue{ RHI::HardwareQueueClass::Graphics });

        passContext.Add<RHI::InputStreamLayout>(uiPass);

        RHI::RenderTargetLayout renderTargetLayout;
        renderTargetLayout.m_colorAttachmentCount = 1;
        renderTargetLayout.m_colorFormats = {RHI::Format::R8G8B8A8_UNORM};
        passContext.Add<RHI::RenderTargetLayout>(uiPass, renderTargetLayout);

        passContext.Add<RHI::RenderStates>(uiPass);
        passContext.Add<PassShaders>(uiPass);
        passContext.Add<PassShaderInputs>(uiPass);

        PassFunctions uiPassFunc;
        uiPassFunc.m_buildFunction = [this](RenderGraphBuilder& builder)
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
        };

        uiPassFunc.m_executeFunction = [this](RHI::CommandList* commandList, RenderGraphExecuter&)
        {
            m_rednerUI.Render(commandList);
        };

        passContext.Add<PassFunctions>(uiPass, uiPassFunc);
    }

    void RenderSystem::InitPipeline()
    {
        // TODO: pipeline state object creation per pass — depends on shader asset wiring
        // (see commented block in git history). Left as no-op until shader pipeline is rebuilt.
    }

    void RenderSystem::ExecutePipeline(Pipeline& pipeline)
    {
        const uint32_t frameIndex = m_rhiData.m_swapChain->GetCurrentImageIndex();
        m_renderGraph.ExecutePipeline(pipeline, frameIndex);
        m_rhiData.m_swapChain->Present();
    }

    void RenderSystem::InitInternal()
    {
        InitRHIData();

        RHIExecuteContext::Push(m_rhiContext);

        // ImportSwapChain materializes swap chain entities into the current RHIContext,
        // so it must run after RHIExecuteContext::Push.
        m_renderGraph.ImportSwapChain(*m_rhiData.m_swapChain);

        InitRenderUI();
        BuildPipeline();
        InitPipeline();

        TickBus::Handler::BusConnect();
    }

    void RenderSystem::ShutdownInternal()
    {
        RHIExecuteContext::Pop();
        TickBus::Handler::BusDisconnect();
    }

    void RenderSystem::OnTick(float deltaTime)
    {
        ExecutePipeline(m_pipeline);
    }
}
