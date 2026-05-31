#include "EditorInput.h"

#include <Log/ILogSystem.h>

namespace Editor
{
    using namespace Spark;
    using namespace Spark::Input;

    void EditorInputSystem::InitInternal()
    {
        InputEventBus::Handler::BusConnect(InputBusId::Editor);
    }


    void EditorInputSystem::ShutdownInternal()
    {
        if (InputEventBus::Handler::BusIsConnectedId(InputBusId::Editor))
        {
            InputEventBus::Handler::BusDisconnect(InputBusId::Editor);
        }
    }

    void EditorInputSystem::OnMouseButtonEvent(MouseButtonEvent event)
    {
        if (event.button == MouseButton::Left && event.state == InputState::Press)
        {
            LOG_INFO("[Editor] Mouse left click");
        }
        else if (event.button == MouseButton::Right && event.state == InputState::Press)
        {
            LOG_WARN("[Editor] Mouse Right click");
        }
    }

}