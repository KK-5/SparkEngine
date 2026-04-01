#include "InputSystem.h"

#include <Log/SpdLogSystem.h>

#include "GLFWCaptureSystem.h"
#include "Bus/InputEventBus.h"

namespace Spark::Input
{
    void InputSystem::InitInternal()
    {
        m_capturer = CreateSystem<GLFWCaptureSystem>();
        m_capturer->InitInternal();

        TickBus::Handler::BusConnect();
    }

    void InputSystem::ShutdownInternal()
    {
        if (InputEventBus::HasHandlers())
        {
            LOG_WARN("[InputSystem] The input event processing system is not disconnected");
        }

        m_capturer->ShutdownInternal();

        TickBus::Handler::BusDisconnect();
    }

    void InputSystem::OnTick(WorldContext& context, [[maybe_unused]]float deltaTime)
    {
        m_capturer->CaptureWindowEvent(context);
    }
}