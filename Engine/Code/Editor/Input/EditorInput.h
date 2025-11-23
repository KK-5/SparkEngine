#pragma once

#include <ECS/ISystem.h>
#include <ECS/WorldContext.h>
#include <Feature/Input/Bus/InputEventBus.h>

namespace Editor
{
    // 响应输入事件的系统，它会在OnTick的Input阶段被调用
    class EditorInputSystem : public Spark::ISystem,
                              public Spark::Input::InputEventBus::Handler
    {
    public:
        // ISystem
        void Initialize() override;
        void ShutDown() override;

        eastl::vector<HashString> Request() const override
        {
            return {"InputSystem"_hs};
        }

        HashString GetName() const override
        {
            return "EditorInputSystem"_hs;
        }

        // InputEventBus
        void OnMouseButtonEvent(Spark::WorldContext& context, Spark::Input::MouseButtonEvent event) override;

    };
}