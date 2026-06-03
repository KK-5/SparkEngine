#include "BottomPanel.h"

#include <filesystem>
#include <imgui.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <Feature/UI/ImGui/IconManagerInterface.h>


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
            DrawAssets();
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

        if (auto logger = Service<ILogSystem>::Get())
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

    // ============================================================================
    // Asset Browser
    // ============================================================================

    void BottomPanel::LoadIcons()
    {
        if (m_iconsLoaded)
        {
            return;
        }
        m_iconsLoaded = true;

        auto* iconMgr = Spark::Service<Spark::UI::IconManagerInterface>::Get();
        if (!iconMgr)
        {
            return;
        }

        m_folderIconId = iconMgr->OpenIcon("Editor/Asset/folder.png");
        m_fileIconId   = iconMgr->OpenIcon("Editor/Asset/plus-square.png");
    }

    void BottomPanel::ScanDirectory(const eastl::string& path, AssetFolder& folder)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::path dirPath(path.c_str());

        if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
            return;
        }

        fs::directory_iterator it(dirPath, ec);
        fs::directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }

            const fs::path& entryPath = it->path();
            eastl::string entryName(entryPath.filename().string().c_str());
            eastl::string entryFullPath(entryPath.string().c_str());

            if (fs::is_directory(entryPath, ec)) {
                AssetFolder subFolder;
                subFolder.name = eastl::move(entryName);
                subFolder.fullPath = entryFullPath;
                ScanDirectory(entryFullPath, subFolder);
                folder.children.push_back(eastl::move(subFolder));
            } else {
                FileEntry file;
                file.name = eastl::move(entryName);
                file.fullPath = entryFullPath;
                file.isDirectory = false;
                folder.files.push_back(eastl::move(file));
            }
        }
    }

    void BottomPanel::DrawFolderTree(const AssetFolder& folder)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                 | ImGuiTreeNodeFlags_SpanFullWidth;

        if (folder.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (m_selectedFolder == &folder) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        auto* iconMgr = Spark::Service<Spark::UI::IconManagerInterface>::Get();
        ImTextureID folderIcon = ImTextureID_Invalid;
        if (iconMgr && m_folderIconId.IsValid())
        {
            folderIcon = iconMgr->RequestIconId(m_folderIconId);
        }

        if (folderIcon != ImTextureID_Invalid)
        {
            ImGui::Image(folderIcon, ImVec2(16, 16));
            ImGui::SameLine();
        }

        bool open = ImGui::TreeNodeEx(folder.name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_selectedFolder = &folder;
        }

        if (open) {
            for (const auto& child : folder.children) {
                DrawFolderTree(child);
            }
            ImGui::TreePop();
        }
    }

    void BottomPanel::DrawAssetList()
    {
        if (m_selectedFolder == nullptr) {
            ImGui::TextUnformatted("Select a folder to browse assets.");
            return;
        }

        ImGui::BeginChild("AssetFiles", ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoScrollWithMouse);

        const char* filter = m_filterBuf.empty() ? nullptr : m_filterBuf.data();

        auto* iconMgr = Spark::Service<Spark::UI::IconManagerInterface>::Get();
        ImTextureID fileIcon = ImTextureID_Invalid;
        if (iconMgr && m_fileIconId.IsValid())
        {
            fileIcon = iconMgr->RequestIconId(m_fileIconId);
        }

        if (m_selectedFolder->files.empty()) {
            ImGui::TextUnformatted("(empty)");
        }

        for (const auto& file : m_selectedFolder->files) {
            if (filter && filter[0] != '\0') {
                if (file.name.find(filter) == eastl::string::npos) {
                    continue;
                }
            }

            // 根据扩展名确定资产类型图标
            const char* typeLabel = "";
            ImVec4      typeColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            auto extPos = file.name.rfind('.');
            if (extPos != eastl::string::npos) {
                eastl::string ext(file.name.data() + extPos, file.name.size() - extPos);

                if (ext == ".hlsl" || ext == ".hlsli") {
                    typeLabel = "[Shader]";
                    typeColor = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                } else if (ext == ".png" || ext == ".jpg" || ext == ".tga"
                        || ext == ".bmp" || ext == ".hdr") {
                    typeLabel = "[Image]";
                    typeColor = ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
                } else if (ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                    typeLabel = "[Model]";
                    typeColor = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
                }
            }

            if (fileIcon != ImTextureID_Invalid)
            {
                ImGui::Image(fileIcon, ImVec2(16, 16));
                ImGui::SameLine();
            }

            ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
            ImGui::Selectable(typeLabel, false);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::Selectable(file.name.c_str(), false);
        }

        ImGui::EndChild();
    }

    void BottomPanel::DrawAssets()
    {
        if (!m_treeBuilt) {
            m_treeBuilt = true;

            LoadIcons();

            m_searchRoot = "SandBox/Asset";
            m_rootFolder.name = "Assets";
            m_rootFolder.fullPath = m_searchRoot;
            ScanDirectory(m_searchRoot, m_rootFolder);
            m_selectedFolder = &m_rootFolder;
        }

        // ---- 顶部工具栏 ----
        float toolbarHeight = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("AssetToolbar", ImVec2(0, toolbarHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushItemWidth(200.0f);
        if (m_filterBuf.empty()) {
            m_filterBuf.resize(128, '\0');
        }
        ImGui::SetNextItemShortcut(ImGuiKey_F | ImGuiMod_Ctrl);
        if (ImGui::InputTextWithHint("##filter", "Search...", m_filterBuf.data(), m_filterBuf.size())) {
            // 每次输入时自动更新过滤
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();

        if (m_selectedFolder != nullptr) {
            ImGui::TextUnformatted(m_selectedFolder->fullPath.c_str());
        }

        ImGui::EndChild();

        ImGui::Separator();

        // ---- 主体：左侧目录树 + 右侧资产列表 ----
        float leftWidth = ImGui::GetContentRegionAvail().x * 0.3f;

        ImGui::BeginChild("FolderTree", ImVec2(leftWidth, 0), false,
                          ImGuiWindowFlags_NoScrollWithMouse);
        DrawFolderTree(m_rootFolder);
        ImGui::EndChild();

        ImGui::SameLine();

        // 垂直分割线
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        float  lineH = ImGui::GetContentRegionAvail().y;
        drawList->AddLine(ImVec2(cursor.x, cursor.y),
                          ImVec2(cursor.x, cursor.y + lineH),
                          IM_COL32(80, 80, 80, 255), 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(cursor.x + 4, cursor.y));

        ImGui::BeginChild("AssetContent", ImVec2(0, 0), false);
        DrawAssetList();
        ImGui::EndChild();
    }

}