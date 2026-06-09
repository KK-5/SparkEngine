#include "SceneView.h"

#include <imgui.h>

#include <Log/ILogSystem.h>

#include <Resource/AssetTypes.h>
#include <Resource/Asset.h>
#include <Resource/Model/ModelAsset.h>

#include <UI/Bus/AssetEditBus.h>

namespace Editor
{
    using namespace Spark;

    void SceneView::Draw()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar 
                                | ImGuiWindowFlags_NoScrollWithMouse
                                | ImGuiWindowFlags_NoTitleBar
                                | ImGuiWindowFlags_NoInputs;
        ImGui::Begin("Scene View", nullptr, flags);

        ImVec2 viewportPos  = ImGui::GetCursorScreenPos();
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        if (viewportSize.x <= 0 || viewportSize.y <= 0)
        {
            ImGui::End();
            return;
        }

        /*
        ImGui::GetWindowDrawList()->AddRectFilled(
            viewportPos,
            ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y),
            IM_COL32(30, 30, 30, 255));
        */

        ImGui::InvisibleButton("SceneViewport", viewportSize);
        ImGui::PushStyleColor(ImGuiCol_DragDropTarget, IM_COL32(0, 0, 0, 0));
        if (ImGui::BeginDragDropTarget())
        {
            auto* payload = ImGui::AcceptDragDropPayload("DRAG_ASSET_FILE");
            if (payload)
            {
                auto* asset = *static_cast<Resource::Asset**>(payload->Data);
                LOG_INFO("[SceneView] Accept asset {}", asset->GetAssetId().GetPath().c_str());
                switch (asset->GetAssetType())
                {
                case Resource::AssetType::Model:
                {
                    auto* modelAsset = static_cast<Resource::ModelAsset*>(asset);
                    AssetEditBus::Broadcast(&AssetEditBus::Events::OnModelAssetDragToScene, *modelAsset);
                    break;
                }
                
                default:
                    break;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopStyleColor();
        ImGui::End();
    }
}