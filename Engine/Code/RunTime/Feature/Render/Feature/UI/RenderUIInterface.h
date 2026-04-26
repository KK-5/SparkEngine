#pragma once

namespace Spark::Render
{
    // 生命周期由外部 UI 系统驱动；RHI 资源由 RenderSystem 在内部预先 Bind
    // RHI未完全集成imgui的临时解决方案，由外部管理ui所需的GPU资源
    class RenderUIInterface
    {
    public:
        virtual ~RenderUIInterface() = default;

        virtual void Init()     = 0;
        virtual void Shutdown() = 0;
        virtual void NewFrame() = 0;
    };
}