#include "ImGui.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Device/Device.h>
#include <RHI/Command/CommandQueue.h>

namespace Spark::RHI
{
    RHI::ResultCode ImGui::Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc)
    {
        m_desc = desc;

        if (commandQueue.GetDescriptor().m_hardwareQueueClass != RHI::HardwareQueueClass::Graphics)
        {
            LOG_ERROR("[ImGui] Require a graphics command queue to initialize ImGui.");
            return ResultCode::InvalidArgument;
        }
        return InitInternal(device, commandQueue, desc);
    }

    const ImGuiDescriptor& ImGui::GetDescriptor() const
    {
        return m_desc;
    }
}