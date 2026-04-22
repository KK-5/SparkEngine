#include "RenderSystem.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <Log/SpdLogSystem.h>
#include <EASTL/any.h>

#include <RHI/SwapChain/SwapChainDescriptor.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include <RHI/Attachment/RenderAttachmentLayoutBuilder.h>

#include <Pass/Pass.h>
#include <Pass/PassTag.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

#include <thread>

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
        Ptr<RHI::PhysicalDevice> selectDevice;
        for (Ptr<RHI::PhysicalDevice> physicalDev: devList)
        {
            // Now chose the first device
            selectDevice = physicalDev;
            break;
        }

        RHI::DeviceDescriptor deviceDesc;
        deviceDesc.m_frameCountMax = 3;
        m_rhiData.m_device = m_rhiData.m_factory->CreateDevice();
        if (m_rhiData.m_device->Init(*selectDevice, deviceDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Init device failed.");
            return false;
        }

        m_rhiData.m_commandQueuecontext.Init(*m_rhiData.m_device);

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
            m_rhiData.m_commandQueuecontext.GetCommandQueue(RHI::HardwareQueueClass::Graphics),
            desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Create swap chain failed!");
            return false;
        }

        auto& rhiContext = m_rhiContext;
        for (uint32_t i = 0; i < m_rhiData.m_swapChain->GetDescriptor().m_dimensions.m_imageCount; ++i)
        {
            auto image = m_rhiData.m_swapChain->GetImage(i);
            Ptr<RHI::ImageView> imageview = m_rhiData.m_factory->CreateImageView();
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
            RHIHandle swapchainHandle = rhiContext.CreateEntity();
            rhiContext.Add<Ptr<RHI::ImageView>>(swapchainHandle, eastl::move(imageview));
            rhiContext.Add<SwapChainView>(swapchainHandle, i);
            // rhiContext.Add<PassTag<uiPass>>(swapchainHandle);
        }

        return true;
    }

    bool RenderSystem::InitRenderUI()
    {
        RHI::ImGuiDescriptor desc;
        desc.m_rtvFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.m_dsvFormat = RHI::Format::D32_FLOAT;
        m_rednerUI.Init(*m_rhiData.m_device, m_rhiData.m_commandQueuecontext.GetCommandQueue(RHI::HardwareQueueClass::Graphics), desc);
        return true;
    }

    void RenderSystem::BuildPipeline()
    {
        auto& passContext = m_pipeline.GetPassContext();
        
        Pass parentPass = passContext.CreateEntity();
        passContext.Add<PassName>(parentPass, ObjectName("Parent Pass"));
        passContext.Add<ActivePassTag>(parentPass);
        passContext.Add<ParentPassTag>(parentPass);

        RHI::RenderAttachmentLayoutBuilder attachmentBuilder;
        attachmentBuilder.AddSubpass()->RenderTargetAttachment(RHI::Format::R8G8B8A8_UNORM, ObjectName("Color"))
                                      ->DepthStencilAttachment(RHI::Format::D32_FLOAT, ObjectName("Depth"));
        RHI::RenderAttachmentLayout renderAttachmentLayout;
        attachmentBuilder.End(renderAttachmentLayout);
        passContext.Add<RHI::RenderAttachmentLayout>(parentPass, renderAttachmentLayout);



        Pass uiPass = passContext.CreateEntity();
        passContext.Add<PassName>(uiPass, ObjectName("UIPass"));
        passContext.Add<ActivePassTag>(uiPass);
        passContext.Add<RenderPassTag>(uiPass);
        passContext.Add<ParentPassInfo>(uiPass, parentPass, (uint32_t)0);

        PassFunctions uiPassFunc;
        uiPassFunc.m_buildFunction = [](RHIContext& rhiContext)
        {

        };
        uiPassFunc.m_executeFunction = [&](RHI::CommandList* commandList)
        {
            m_rednerUI.Render(commandList);
        };
        passContext.Add<PassFunctions>(uiPass, uiPassFunc);

    }

    void ExecutePipeline(Pipeline& pipeline)
    {
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);



        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
    }

    void RenderSystem::InitInternal()
    {
        TickBus::Handler::BusConnect();
    }

    void RenderSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }

    void RenderSystem::OnTick(float deltaTime)
    {
        // Simple execute



        // ui pass
        int width, height;
        auto size = Service<Window::IWindowSystem>::Get()->GetWindowSize();
        width = size.first;
        height = size.second;

        glViewport(0, 0, width, height);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        eastl::any renderData = Service<UI::UIBaseSystem>::Get()->GetUIRenderData();
        ImDrawData* data = eastl::any_cast<ImDrawData*>(renderData);
        ImGui_ImplOpenGL3_RenderDrawData(data);
    }
}