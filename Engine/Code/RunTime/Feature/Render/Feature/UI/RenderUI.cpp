#include "RenderUI.h"

#include <Log/SpdLogSystem.h>
#include <Service/Service.h>
#include <RHI/RHIInterface.h>
#include <RHI/Device/Device.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/UI/ImGuiDescriptor.h>

namespace Spark::Render
{
    void RenderUI::Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const RHI::ImGuiDescriptor& desc)
    {
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

        RHI::ResultCode result = m_rhiImGUi->Init(device, commandQueue, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderUI] ImGUi initialize failed.");
        }
    }

    void RenderUI::NewFrame()
    {
        if (m_rhiImGUi)
        {
            m_rhiImGUi->NewFrame();
        }
    }

    void RenderUI::Render(ImDrawData* drawData, RHI::CommandList* commandList)
    {
        if (!drawData || !commandList)
        {
            LOG_ERROR("[RenderUI] Invalid imgui render data or rhi commandlist");
            return;
        }

        if (m_rhiImGUi)
        {
            m_rhiImGUi->RenderDrawData(drawData, commandList);
        }
    }
}