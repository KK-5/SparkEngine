#include "RenderSystem.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <Log/SpdLogSystem.h>
#include <EASTL/any.h>

#include <RHI/SwapChain/SwapChainDescriptor.h>
#include <RHI/SwapChain/SwapChain.h>

#include <Pass/Pass.h>
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

        RHI::SwapChainDescriptor desc;
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
        }
    }

    bool RenderSystem::InitRenderUI()
    {
        //m_rednerUI.Init()
    }

    void RenderSystem::InitInternal()
    {



        auto& passContext = m_pipeline.GetPassContext();
        Pass uiPass = passContext.CreateEntity();
        passContext.Add<PassName>(uiPass, "UIPass");
        passContext.Add<ActivePassTag>(uiPass);

        PassFunctions uiPassFunc;
        uiPassFunc.m_executeFunction = [](RHI::CommandList* commandList)
        {
            eastl::any renderData = Service<UI::UIBaseSystem>::Get()->GetUIRenderData();
            ImDrawData* data = eastl::any_cast<ImDrawData*>(renderData);
            if (!data)
            {
                LOG_ERROR("[UI Pass] Imgui render data is null.");
                return;
            }
        };
        

        TickBus::Handler::BusConnect();
    }

    void RenderSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }

    void RenderSystem::OnTick(float deltaTime)
    {



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