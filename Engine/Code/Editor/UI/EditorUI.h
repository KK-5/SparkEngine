#pragma once

#include <EASTL/allocator.h>
#include <EASTL/internal/move_help.h>
#include <EASTL/utility.h>
#include <EASTL/any.h>
#include <imgui.h>

#include <ECS/WorldContext.h>
#include <ECS/ISystem.h>
#include <Service/Service.h>

#include <Feature/UI/ImGui/SparkImGui.h>
#include <Feature/Input/InputEvent.h>
#include <Feature/Input/Bus/InputEventBus.h>

#include "Private/MenuBar.h"
#include "Private/BottomPanel.h"
#include "Private/SceneView.h"
#include "Private/Inspector.h"
#include "Private/ComponentView.h"
#include "Private/MaterialWindow.h"
#include "Private/SaveAssetDialog.h"
#include "Private/WelcomeScreen.h"
#include "Private/WindowChrome.h"

namespace Editor
{
    class EditorUI : public Spark::UI::SparkImGui,
                     public Spark::Input::InputEventBus::Handler
    {
    public:
        explicit EditorUI(WindowChrome& windowChrome) : m_windowChrome(windowChrome) {}

        // SparkImGui
        void InitInternal() override;
        void ShutdownInternal() override;
        void DrawUI() override;

        bool WantCaptureMouse() const override;
        // bool WantCaptureKeyboard() const override;

        Spark::Math::Vector2Int GetFrameBufferSize() const override;
        Spark::Math::Vector2Int GetFrameBufferPos() const override;

        // InputEventBus
        void OnMouseButtonEvent(Spark::Input::MouseButtonEvent event) override;
        void OnMouseCursorPosEvent(Spark::Input::MouseCursorPosEvent event) override;
        void OnMouseScrollEvent(Spark::Input::MouseScrollEvent event) override;
        void OnKeyboardEvent(Spark::Input::KeyboardEvent event) override;

    private:
        void SetupDefaultLayout(ImGuiID dockspaceId);

        bool m_dockLayoutInit;

        //! Kept across frames where the viewport collapses (minimized), since a zero-sized
        //! scene target is not creatable.
        mutable Spark::Math::Vector2Int m_lastFrameBufferSize {1024, 576};

        eastl::unique_ptr<MenuBar>       m_menuBar;
        eastl::unique_ptr<BottomPanel>   m_bottomPanel;
        eastl::unique_ptr<SceneView>     m_sceneView;
        eastl::unique_ptr<Inspector>     m_inspector;
        eastl::unique_ptr<ComponentView> m_componentView;

        //! Not in SetupDefaultLayout: it is free-floating and starts closed, opened from the
        //! material slot's edit icon.
        eastl::unique_ptr<MaterialWindow> m_materialWindow;

        //! Drawn last: it is modal, so it belongs over whatever asked for it.
        eastl::unique_ptr<SaveAssetDialog> m_saveAssetDialog;

        //! Drawn INSTEAD of everything above until dismissed; owns the startup preload.
        eastl::unique_ptr<WelcomeScreen> m_welcomeScreen;

        //! Owned by SparkEditor, which installs it as soon as the window exists.
        WindowChrome& m_windowChrome;
    };
}