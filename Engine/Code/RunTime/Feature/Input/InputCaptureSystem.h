#pragma once

#include <EASTL/vector.h>
#include <EASTL/functional.h>
#include <EASTL/optional.h>

#include <ECS/ISystem.h>
#include <ECS/WorldContext.h>
#include <Tick/TickBus.h>
#include <HashString/HashString.h>

#include "InputEvent.h"

namespace Spark::Input
{
    class InputCaptureSystem : public ISystem
    {
    public:
        virtual ~InputCaptureSystem() = default;

        // ISystem
        void Initialize() override;
        void ShutDown() override;

        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs, "WindowSystem"_hs};
        }

        HashString GetName() const override
        {
            return "InputCaptureSystem"_hs;
        }

        //////////////////////
        void CaptureWindowEvent(WorldContext& context);

    protected:
        virtual void CaptureMouseButtonEvent()     = 0;
        virtual void CaptureMouseCursorPosEvent()  = 0;
        virtual void CaptureMouseScrollEvent()     = 0;
        virtual void CaptureKeyboardEvent()        = 0;
        virtual void CaptureWindowCloseEvent()     = 0;
        virtual void CaptureWindowResizeEvent()    = 0;

        eastl::optional<eastl::reference_wrapper<WorldContext>> m_contextRef;
    };
}