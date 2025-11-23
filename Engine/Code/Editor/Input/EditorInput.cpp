#include "EditorInput.h"

#include <Log/SpdLogSystem.h>

namespace Editor
{
    using namespace Spark;
    using namespace Spark::Input;

    void EditorInputSystem::Initialize()
    {
        InputEventBus::Handler::BusConnect(InputBusId::Editor);
    }


    void EditorInputSystem::ShutDown()
    {
        if (InputEventBus::Handler::BusIsConnectedId(InputBusId::Editor))
        {
            InputEventBus::Handler::BusDisconnect(InputBusId::Editor);
        }
    }

    void EditorInputSystem::OnMouseButtonEvent(WorldContext& context, MouseButtonEvent event)
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