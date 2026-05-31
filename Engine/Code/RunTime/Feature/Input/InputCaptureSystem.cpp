#include "InputCaptureSystem.h"

#include <EASTL/functional.h>
#include <Log/ILogSystem.h>

#include "../Window/IWindowSystem.h"

namespace Spark::Input
{
    void InputCaptureSystem::InitInternal()
    {
        if (!Service<Spark::Window::IWindowSystem>::Get())
        {
            LOG_ERROR("[InputCaptureSystem] IWindowSystem is invalid");
            assert(false);
        }
    }

    void InputCaptureSystem::CaptureWindowEvent()
    {
        Service<Window::IWindowSystem>::Get()->PollEvents();
    }

    void InputCaptureSystem::ShutdownInternal()
    {

    }
}