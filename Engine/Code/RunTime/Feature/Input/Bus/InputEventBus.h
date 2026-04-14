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
        virtual void OnMouseButtonEvent(MouseButtonEvent event) {};
        virtual void OnMouseCursorPosEvent(MouseCursorPosEvent event) {};
        virtual void OnMouseScrollEvent(MouseScrollEvent event) {};
        virtual void OnKeyboardEvent(KeyboardEvent event) {};
        virtual void OnWindowCloseEvnet() {};
        virtual void OnWindowResizeEvent(WindowResizeEvent event) {};
    };

    using InputEventBus = EBus<InputEvents>;
}