#pragma once

#include <Service/Service.h>
#include <UI/UIBaseSystem.h>

struct ImFont;

namespace Spark::UI
{
    //! The typefaces the UI is drawn with, by role rather than by index into io.Fonts.
    //!
    //! Size is not part of the identity: since imgui 1.92 a font is baked on demand at
    //! whatever size PushFont asks for, so one file per WEIGHT is the whole set -- the
    //! sizes live with the layout that chooses them.
    //!
    //! Null until the UI system has initialised. Callers that can run before that must
    //! check; passing null to PushFont means "keep the current font", which is a sane
    //! enough fallback that it is not worth asserting over.
    namespace Fonts
    {
        ImFont* UI();
        ImFont* Bold();
        ImFont* Mono();
    }

    class SparkImGui : public Service<UIBaseSystem>::Handler
    {
    public:
        void InitInternal() override;
        void ShutdownInternal() override;

        void NewFrame() override;
        void EndFrame() override;

        eastl::any RenderUI() override;

        bool WantCaptureMouse() const override;
        bool WantCaptureKeyboard() const override;

    };
}