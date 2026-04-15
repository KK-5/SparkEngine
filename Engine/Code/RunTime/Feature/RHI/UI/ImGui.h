#pragma once

#include <imgui.h>

#include <RHI/Base.h>
#include "ImGuiDescriptor.h"

namespace Spark::RHI
{
    class Device;
    class CommandList;
    class CommandQueue;

    class ImGui
    {
    public:
        virtual ~ImGui() = default;

        RHI::ResultCode Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc);

        virtual void Shutdown() = 0;
        virtual void NewFrame() = 0;
        virtual void RenderDrawData(ImDrawData* drawData, RHI::CommandList* commandList) = 0;

        const ImGuiDescriptor& GetDescriptor() const;
    
    private:
        virtual RHI::ResultCode InitInternal(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc) = 0;

        ImGuiDescriptor m_desc;
    };
}
