#include "InputCaptureSystem.h"

#include <EASTL/functional.h>
#include <Log/SpdLogSystem.h>

#include "../Window/IWindowSystem.h"

namespace Spark::Input
{
    void InputCaptureSystem::Initialize()
    {
        if (!Service<Spark::Window::IWindowSystem>::Get())
        {
            LOG_ERROR("[InputCaptureSystem] IWindowSystem is invalid");
            assert(false);
        }
    }

    void InputCaptureSystem::CaptureWindowEvent(WorldContext& context)
    {
        m_contextRef = eastl::ref(context);
        Service<Window::IWindowSystem>::Get()->PollEvents();
    }

    void InputCaptureSystem::ShutDown()
    {

    }
}