#pragma once

#include <ECS/WorldContext.h>
#include <EBus/EBus.h>

#include "../InputEvent.h"

namespace Spark::Input
{
    enum class InputBusId
    {
        EditorUI,
        Editor,
        GameUI,
        Game
    };

    class InputEvents : public EBusTraits
    {
    public:
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Single;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ByIdAndOrdered;

        using BusIdType = InputBusId;
        using BusIdOrderCompare = eastl::less<InputBusId>;
    public:
        // 非纯虚函数，子类可以选择响应哪些事件
        virtual void OnMouseButtonEvent(WorldContext& context, MouseButtonEvent event) {};
        virtual void OnMouseCursorPosEvent(WorldContext& context, MouseCursorPosEvent event) {};
        virtual void OnMouseScrollEvent(WorldContext& context, MouseScrollEvent event) {};
        virtual void OnKeyboardEvent(WorldContext& context, KeyboardEvent event) {};
        virtual void OnWindowCloseEvnet(WorldContext& context) {};
        virtual void OnWindowResizeEvent(WorldContext& context, WindowResizeEvent event) {};
    };

    using InputEventBus = EBus<InputEvents>;
}