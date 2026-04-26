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

namespace Editor
{
    class EditorUI : public Spark::UI::SparkImGui,
                     public Spark::Input::InputEventBus::Handler
    {
    public:
        // SparkImGui
        void InitInternal() override;
        void ShutdownInternal() override;
        void DrawUI() override;

        // InputEventBus
        void OnMouseButtonEvent(Spark::Input::MouseButtonEvent event) override;
        void OnMouseCursorPosEvent(Spark::Input::MouseCursorPosEvent event) override;
        void OnMouseScrollEvent(Spark::Input::MouseScrollEvent event) override;
        void OnKeyboardEvent(Spark::Input::KeyboardEvent event) override;

    private:
        void SetupDefaultLayout(ImGuiID dockspaceId);

        bool m_dockLayoutInit;

        eastl::unique_ptr<MenuBar>       m_menuBar;
        eastl::unique_ptr<BottomPanel>   m_bottomPanel;
        eastl::unique_ptr<SceneView>     m_sceneView;
        eastl::unique_ptr<Inspector>     m_inspector;
        eastl::unique_ptr<ComponentView> m_componentView;
    };
}