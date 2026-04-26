#include "RenderUI.h"

#include <EASTL/any.h>

#include <Log/SpdLogSystem.h>
#include <Service/Service.h>
#include <RHI/RHIInterface.h>
#include <RHI/Device/Device.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/UI/ImGuiDescriptor.h>

#include <UI/UIBaseSystem.h>

namespace Spark::Render
{

    RenderUI::~RenderUI()
    {
        Shutdown();
    }

    void RenderUI::Bind(RHI::Device& device, RHI::CommandQueue& commandQueue, const RHI::ImGuiDescriptor& desc)
    {
        m_device       = &device;
        m_commandQueue = &commandQueue;
        m_desc         = desc;
    }

    void RenderUI::Init()
    {
        if (m_initialized)
        {
            return;
        }

        ASSERT(m_device && m_commandQueue, "[RenderUI] Bind must be called before Init.");

        auto rhi = Spark::Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "RHI is invalid.");
        auto factory = rhi->GetRHIFactory();
        ASSERT(factory, "RHI factory is invalid.");

        m_rhiImGUi = factory->CreateRHIImGui();
        if (!m_rhiImGUi)
        {
            LOG_ERROR("[RenderUI] Create RHI imgui failed.");
            return;
        }

        RHI::ResultCode result = m_rhiImGUi->Init(*m_device, *m_commandQueue, m_desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderUI] ImGUi initialize failed.");
            m_rhiImGUi.reset();
            return;
        }

        m_initialized = true;
    }

    void RenderUI::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }
        if (m_commandQueue)
        {
            m_commandQueue->WaitForIdle();
        }

        m_rhiImGUi.reset();
        m_initialized = false;
    }

    void RenderUI::NewFrame()
    {
        ASSERT(m_rhiImGUi, "[RenderUI] RHI imgui is invalid.");
        m_rhiImGUi->NewFrame();
    }

    void RenderUI::Render(RHI::CommandList* commandList)
    {
        if (!commandList)
        {
            LOG_ERROR("[RenderUI] Invalid rhi commandlist");
            return;
        }

        auto ui = Service<UI::UIBaseSystem>::Get();
        if (!ui)
        {
            LOG_ERROR("[RenderUI] Invalid imgui window");
            return;
        }
        auto drawData = ui->RenderUI();
        ImDrawData* imguiDrawData = eastl::any_cast<ImDrawData*>(drawData);
        if (m_rhiImGUi && imguiDrawData)
        {
            m_rhiImGUi->RenderDrawData(imguiDrawData, commandList);
        }
    }
}