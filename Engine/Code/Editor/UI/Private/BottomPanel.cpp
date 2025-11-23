#include "BottomPanel.h"

#include <imgui.h>

#include <Log/SpdLogSystem.h>


namespace Editor
{

    void BottomPanel::Draw()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;

        ImGui::Begin("Browser", nullptr, flags);

        // 标签选择：Console 或 Assets
        if (ImGui::Button("Console")) {
            currentTab = Tab::CONSILE;
        }
        ImGui::SameLine();
        if (ImGui::Button("Assets")) {
            currentTab = Tab::ASSETS;
        }
        
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(35, 35, 35, 255));
        if (currentTab == Tab::CONSILE) 
        {
            DrawConsole();
        } else {
            //DrawAssets();
        }
        ImGui::PopStyleColor();
        
        ImGui::End();
    }

    void BottomPanel::DrawConsole()
    {
        using namespace Spark;

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::BeginChild("ConsoleLog", ImVec2(0, 0), true);

        auto GetLogColor = [](const std::string& log) -> ImVec4
        {
            if (log.find("[trace]") != std::string::npos)
            {
                return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            }
            else if (log.find("[debug]") != std::string::npos)
            {
                return ImVec4(0.3f, 0.8f, 1.0f, 1.0f);
            }
            else if (log.find("[info]") != std::string::npos)
            {
                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            else if (log.find("[warning]") != std::string::npos)
            {
                return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            }
            else if (log.find("[error]") != std::string::npos)
            {
                return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            }
            else if (log.find("[critical]") != std::string::npos)
            {
                return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            else
            {
                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        };

        if (auto logger = Service<ILogSystem<SpdLogSystem>>::Get())
        {
            auto logs = logger->GetLogs();
            for (const auto& log : logs) {
                ImGui::PushStyleColor(ImGuiCol_Text, GetLogColor(log));
                ImGui::TextUnformatted(log.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        
        ImGui::EndChild();
    }
}