#include "SceneView.h"

#include <imgui.h>

#include <Log/ILogSystem.h>

#include <Resource/AssetTypes.h>
#include <Resource/Asset.h>
#include <Resource/Model/ModelAsset.h>

#include <UI/Bus/AssetEditBus.h>

#include "FieldWidgets.h"

namespace Editor
{
    using namespace Spark;

    void SceneView::Draw()
    {
        // Takes mouse input, unlike most of this window's siblings, and that is the point:
        // a window carrying ImGuiWindowFlags_NoMouseInputs is skipped by FindHoveredWindow,
        // so imgui could never name the scene as the hovered window and "is the cursor over
        // the viewport" had to be guessed from a rectangle -- which cannot know what floats
        // on top of it. Nav input stays off; the scene is not a place to Tab through.
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                                | ImGuiWindowFlags_NoScrollWithMouse
                                | ImGuiWindowFlags_NoTitleBar
                                | ImGuiWindowFlags_NoNavInputs
                                | ImGuiWindowFlags_NoNavFocus
                                | ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoBackground;
        ImGui::Begin("Scene View", nullptr, flags);

        m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        ImVec2 viewportPos  = ImGui::GetCursorScreenPos();
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (viewportSize.x <= 0 || viewportSize.y <= 0)
        {
            m_hovered = false;
            ImGui::End();
            return;
        }

        // Dummy, not InvisibleButton: all this item is for is giving the drop target below a
        // rectangle (BeginDragDropTarget derives an id from it when the item has none). A
        // button would additionally take the click and become the active id, and an active
        // id makes IsWindowHovered false -- which would cut the camera off the moment the
        // user pressed the mouse to rotate it.
        ImGui::Dummy(viewportSize);
        ImGui::PushStyleColor(ImGuiCol_DragDropTarget, IM_COL32(0, 0, 0, 0));
        // Unknown: the scene takes any asset and decides per type below.
        if (const Resource::Asset* asset = AcceptAssetDrop(Resource::AssetType::Unknown))
        {
            LOG_INFO("[SceneView] Accept asset {}", asset->GetAssetId().GetPath().c_str());
            switch (asset->GetAssetType())
            {
            case Resource::AssetType::Model:
            {
                const auto* modelAsset = static_cast<const Resource::ModelAsset*>(asset);
                AssetEditBus::Broadcast(&AssetEditBus::Events::OnModelAssetDragToScene, *modelAsset);
                break;
            }

            default:
                break;
            }
        }
        ImGui::PopStyleColor();
        ImGui::End();
    }
}