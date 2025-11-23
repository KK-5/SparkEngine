#include "InputSystem.h"

#include <Log/SpdLogSystem.h>

#include "GLFWCaptureSystem.h"
#include "Bus/InputEventBus.h"

namespace Spark::Input
{
    void InputSystem::Initialize()
    {
        m_capturer = eastl::make_unique<GLFWCaptureSystem>();
        m_capturer->Initialize();

        if (!InputEventBus::HasHandlers())
        {
            LOG_WARN("[InputSystem] There is no system handling input events");
        }

        TickBus::Handler::BusConnect();
    }

    void InputSystem::ShutDown()
    {
        if (InputEventBus::HasHandlers())
        {
            LOG_WARN("[InputSystem] The input event processing system is not disconnected");
        }

        m_capturer->ShutDown();

        TickBus::Handler::BusDisconnect();
    }

    void InputSystem::OnTick(WorldContext& context, [[maybe_unused]]float deltaTime)
    {
        m_capturer->CaptureWindowEvent(context);
    }
}