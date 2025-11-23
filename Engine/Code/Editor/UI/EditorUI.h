#pragma once

#include <EASTL/allocator.h>
#include <EASTL/internal/move_help.h>
#include <EASTL/utility.h>
#include <EASTL/any.h>
#include <imgui.h>

#include <ECS/WorldContext.h>
#include <ECS/ISystem.h>
#include <Service/Service.h>

#include <Feature/Window/IWindowSystem.h>
#include <Feature/UI/UIBaseSystem.h>
#include <Feature/Input/InputEvent.h>
#include <Feature/Input/Bus/InputEventBus.h>

#include "Private/MenuBar.h"
#include "Private/BottomPanel.h"
#include "Private/SceneView.h"
#include "Private/Inspector.h"
#include "Private/ComponentView.h"

namespace Editor
{
    class EditorUI : public Spark::Service<Spark::UI::UIBaseSystem>::Handler,
                     public Spark::Input::InputEventBus::Handler
    {
    public:
        // ISystem
        void Initialize() override;
        void ShutDown() override;

        // UIBaseSystem
        void NewFrame() override;
        void DrawUI(Spark::WorldContext& context) override;
        void EndFrame() override;

        eastl::any GetUIRenderData() override;

        bool WantCaptureMouse() const override;
        bool WantCaptureKeyboard() const override; 

        // InputEventBus
        void OnMouseButtonEvent(Spark::WorldContext& context, Spark::Input::MouseButtonEvent event) override;
        void OnMouseCursorPosEvent(Spark::WorldContext& context, Spark::Input::MouseCursorPosEvent event) override;
        void OnMouseScrollEvent(Spark::WorldContext& context, Spark::Input::MouseScrollEvent event) override;
        void OnKeyboardEvent(Spark::WorldContext& context, Spark::Input::KeyboardEvent event) override;

    private:
        void SetUpStyle();
        void SetupDefaultLayout(ImGuiID dockspaceId);

        bool m_dockLayoutInit;

        eastl::unique_ptr<MenuBar>       m_menuBar;
        eastl::unique_ptr<BottomPanel>   m_bottomPanel;
        eastl::unique_ptr<SceneView>     m_sceneView;
        eastl::unique_ptr<Inspector>     m_inspector;
        eastl::unique_ptr<ComponentView> m_componentView;
    };
}