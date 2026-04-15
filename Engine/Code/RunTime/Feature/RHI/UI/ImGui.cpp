#include "ImGui.h"

#include <RHI/Device/Device.h>
#include <RHI/Command/CommandQueue.h>

namespace Spark::RHI
{
    RHI::ResultCode ImGui::Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc)
    {
        m_desc = desc;

        return InitInternal(device, commandQueue, desc);
    }

    const ImGuiDescriptor& ImGui::GetDescriptor() const
    {
        return m_desc;
    }
}