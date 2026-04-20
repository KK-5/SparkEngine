#pragma once

#include <Base.h>
#include <Service/Service.h>
#include <RHI/UI/ImGui.h>
#include "RenderUIInterface.h"

namespace Spark::Render
{
    class RHI::Device;
    class RHI::CommandQueue;
    class RHI::ImGuiDescriptor;
    class RHI::CommandList;

    class RenderUI : public Service<RenderUIInterface>::Handler
    {
    public:
        virtual ~RenderUI() = default;

        void Init(RHI::Device& device, RHI::CommandQueue& commandQueue, const RHI::ImGuiDescriptor& desc);

        void NewFrame() override;

        void Render(RHI::CommandList* commandList);

    private:
        UniquePtr<RHI::ImGui> m_rhiImGUi;
    };
}