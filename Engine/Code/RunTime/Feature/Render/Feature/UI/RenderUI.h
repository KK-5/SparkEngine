#pragma once

#include <Base.h>
#include <Service/Service.h>
#include <RHI/UI/ImGui.h>
#include <RHI/UI/ImGuiDescriptor.h>
#include "RenderUIInterface.h"

namespace Spark::Render
{
    class RHI::Device;
    class RHI::CommandQueue;
    class RHI::CommandList;

    class UIProcessFeature;

    class RenderUI : public Service<RenderUIInterface>::Handler
    {
        friend class UIProcessFeature;

    public:
        virtual ~RenderUI();

        // 由 RenderSystem 在 RHI 资源就绪后调用，把后端依赖塞进来。
        // 不会启动任何 GPU 资源，只缓存指针/描述符。
        void Bind(RHI::Device& device, RHI::CommandQueue& commandQueue, const RHI::ImGuiDescriptor& desc);

        // 以下三个接口由外部 UI 系统按需调度。
        void Init() override;
        void Shutdown() override;
        void NewFrame() override;

        void Render(RHI::CommandList* commandList);

    private:
        RHI::Device*         m_device       = nullptr;
        RHI::CommandQueue*   m_commandQueue = nullptr;
        RHI::ImGuiDescriptor m_desc {};

        UniquePtr<RHI::ImGui> m_rhiImGUi;
        bool m_initialized = false;
    };
}