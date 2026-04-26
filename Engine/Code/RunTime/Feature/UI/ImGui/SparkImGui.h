#pragma once

#include <Service/Service.h>
#include <UI/UIBaseSystem.h>

namespace Spark::UI
{
    class SparkImGui : public Service<UIBaseSystem>::Handler
    {
    public:
        void InitInternal() override;
        void ShutdownInternal() override;

        void NewFrame() override;
        void EndFrame() override;

        eastl::any RenderUI() override;

        bool WantCaptureMouse() const override;
        bool WantCaptureKeyboard() const override;

    };
}