#include "BottomPanel.h"

#include <imgui.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <Resource/AssetManagerInterface.h>
#include <VFS/FileSystem.h>
#include <Feature/UI/ImGui/IconManagerInterface.h>
#include <Feature/Material/MaterialContext.h>
#include <Feature/Material/MaterialUtils.h>

#include "UI/Bus/MaterialEditBus.h"


namespace
{
    eastl::string GetAssetDisplayName(const Spark::Resource::AssetId& id)
    {
        const auto& path = id.GetPath();
        auto pos = path.rfind('/');
        if (pos == eastl::string::npos)
        {
            pos = path.rfind('\\');
        }
        if (pos != eastl::string::npos)
        {
            return eastl::string(path.data() + pos + 1, path.size() - pos - 1);
        }
        return path;
    }
}

namespace Editor
{
    BottomPanel::BottomPanel()
    {
        Spark::FileEventBus::Handler::BusConnect();
    }

    BottomPanel::~BottomPanel()
    {
        if (BusIsConnected())
        {
            BusDisconnect();
        }
    }

    void BottomPanel::OnFileAdded(eastl::string virtualPath)
    {
        // Or saving a note next to the textures would walk every mount.
        auto* assetManager = Spark::Service<Spark::Resource::AssetManager>::Get();
        if (!assetManager || assetManager->GetSupportAssetType(virtualPath)
                                 == Spark::Resource::AssetType::Unknown)
        {
            return;
        }
        m_treeBuilt = false;
    }

    void BottomPanel::OnFileRemoved(eastl::string virtualPath)
    {
        // Unfiltered, unlike additions: what type it WAS can no longer be asked.
        (void)virtualPath;
        m_treeBuilt = false;
    }

    void BottomPanel::OnFileWatchOverflow()
    {
        m_treeBuilt = false;
    }

    void BottomPanel::Draw()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;

        ImGui::Begin("Browser", nullptr, flags);

        LoadIcons();

        // ---- Tab bar ----
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 barStart = ImGui::GetCursorScreenPos();
            float  barH = 26;
            float  tabH = 22;
            float  tabPadX = 14;

            dl->AddRectFilled(barStart,
                ImVec2(barStart.x + ImGui::GetContentRegionAvail().x, barStart.y + barH),
                IM_COL32(35, 35, 35, 255));

            auto* iconMgr = Spark::Service<Spark::UI::IconManagerInterface>::Get();

            struct TabDef { const char* name; const char* id; Tab tab; Spark::Resource::AssetId iconId; };
            TabDef tabs[] = {
                {"Console", "##TabConsole", Tab::CONSILE, m_consoleIconId},
                {"Assets",  "##TabAssets",  Tab::ASSETS,  m_assetsIconId},
            };

            const float iconSize = 16;
            const float iconGap  = 6;
            float x = barStart.x + 8;
            float y = barStart.y + (barH - tabH) * 0.5f;

            for (auto& t : tabs) {
                ImTextureID icon = ImTextureID_Invalid;
                if (iconMgr && t.iconId.IsValid()) {
                    icon = iconMgr->RequestIconId(t.iconId);
                }

                ImVec2 textSize = ImGui::CalcTextSize(t.name);
                bool   hasIcon  = (icon != ImTextureID_Invalid);
                float  w = textSize.x + tabPadX * 2;
                if (hasIcon) {
                    w += iconSize + iconGap;
                }
                ImVec2 tabMin(x, y);
                ImVec2 tabMax(x + w, y + tabH);

                bool sel     = (currentTab == t.tab);
                bool hovered = ImGui::IsMouseHoveringRect(tabMin, tabMax);
                bool held    = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

                ImU32 bg = 0;
                if (held || sel)      { bg = IM_COL32(66, 150, 250, 128); }
                else if (hovered)     { bg = IM_COL32(65, 65, 65, 255); }

                if (bg != 0) {
                    dl->AddRectFilled(tabMin, tabMax, bg, 4);
                }

                float contentX = tabMin.x + tabPadX;
                if (hasIcon) {
                    ImVec2 iconPos(contentX, tabMin.y + (tabH - iconSize) * 0.5f);
                    dl->AddImage(icon, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize));
                    contentX += iconSize + iconGap;
                }

                ImVec2 textPos(contentX, tabMin.y + (tabH - textSize.y) * 0.5f);
                dl->AddText(textPos, IM_COL32(220, 220, 220, 255), t.name);

                ImGui::SetCursorScreenPos(tabMin);
                if (ImGui::InvisibleButton(t.id, ImVec2(w, tabH))) {
                    currentTab = t.tab;
                }

                x += w + 8;
            }

            ImGui::SetCursorScreenPos(ImVec2(barStart.x, barStart.y + barH));
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

        m_folderIconId  = iconMgr->OpenIcon("editor://folder.svg");
        m_fileIconId    = iconMgr->OpenIcon("editor://plus-square.svg");
        m_consoleIconId = iconMgr->OpenIcon("editor://Console.svg");
        m_assetsIconId  = iconMgr->OpenIcon("editor://Assets.svg");
        m_searchIconId  = iconMgr->OpenIcon("editor://search.svg");
        m_unloadIconId  = iconMgr->OpenIcon("editor://unload.svg");
        m_loadingIconId = iconMgr->OpenIcon("editor://loading.svg");
    }

    void BottomPanel::ScanDirectory(const eastl::string& virtualPath, AssetFolder& folder)
    {
        auto* fileSystem = Spark::Service<Spark::FileSystem>::Get();
        if (!fileSystem) {
            return;
        }

        auto* am = Spark::Service<Spark::Resource::AssetManager>::Get();

        fileSystem->ListDirectory(virtualPath,
            [&](eastl::string_view path, bool isDirectory)
        {
            eastl::string entryVirtualPath(path.data(), path.size());

            const size_t slash = entryVirtualPath.rfind('/');
            eastl::string entryName = (slash == eastl::string::npos)
                                    ? entryVirtualPath
                                    : entryVirtualPath.substr(slash + 1);

            if (isDirectory) {
                AssetFolder subFolder;
                subFolder.name = eastl::move(entryName);
                subFolder.fullPath = entryVirtualPath;
                ScanDirectory(entryVirtualPath, subFolder);
                folder.children.push_back(eastl::move(subFolder));
            } else if (am) {
                Spark::Resource::AssetId id = am->MakeAssetId(entryVirtualPath);
                if (id.IsValid() && am->FindAsset(id)) {
                    AssetEntry entry;
                    entry.id = id;
                    entry.type = am->GetSupportAssetType(entryName);
                    folder.assets.push_back(eastl::move(entry));
                }
            }
        });
    }

    void BottomPanel::DrawFolderTree(const AssetFolder& folder)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                 | ImGuiTreeNodeFlags_SpanFullWidth;

        if (folder.children.empty()) {
            // flags |= ImGuiTreeNodeFlags_Leaf;
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

        ImVec2 lineStart = ImGui::GetCursorScreenPos();
        // Reserve space for icon to the left of TreeNodeEx
        ImGui::SetCursorPosX(lineStart.x + 8);

        bool isSelected = (m_selectedFolder == &folder);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            isSelected ? IM_COL32(66, 150, 250, 128) : IM_COL32(65, 65, 65, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(66, 150, 250, 128));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(66, 150, 250, 128));

        bool open = ImGui::TreeNodeEx(folder.name.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_selectedFolder = &folder;
            m_selectedAsset = nullptr;
        }

        // Draw icon after TreeNodeEx so hover highlight doesn't cover it
        if (folderIcon != ImTextureID_Invalid)
        {
            float textH = ImGui::GetTextLineHeight();
            ImVec2 iconPos(lineStart.x, lineStart.y + (textH - 16) * 0.5f);
            ImGui::GetWindowDrawList()->AddImage(
                folderIcon, iconPos, ImVec2(iconPos.x + 16, iconPos.y + 16));
        }

        if (open) {
            for (const auto& child : folder.children) {
                DrawFolderTree(child);
            }
            for (const auto& asset : folder.assets)
            {
                ImGuiTreeNodeFlags fileFlags = ImGuiTreeNodeFlags_Leaf
                                             | ImGuiTreeNodeFlags_SpanFullWidth;

                bool fileIsSelected = (m_selectedAsset == &asset);
                if (fileIsSelected) {
                    fileFlags |= ImGuiTreeNodeFlags_Selected;
                }

                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                    fileIsSelected ? IM_COL32(66, 150, 250, 128) : IM_COL32(65, 65, 65, 255));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(66, 150, 250, 128));
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(66, 150, 250, 128));

                eastl::string displayName = GetAssetDisplayName(asset.id);
                ImGui::TreeNodeEx(displayName.c_str(), fileFlags);

                if (ImGui::IsItemClicked()) {
                    m_selectedAsset = &asset;
                    m_selectedFolder = nullptr;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    OpenAsset(asset);
                }

                ImGui::PopStyleColor(3);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
         ImGui::PopStyleColor(3);
    }

    void BottomPanel::DrawAssetList()
    {
        if (m_selectedFolder == nullptr && m_selectedAsset == nullptr) {
            ImGui::TextUnformatted("Select a folder to browse assets.");
            return;
        }

        ImGui::BeginChild("AssetFiles", ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoScrollWithMouse);

        auto* iconMgr = Spark::Service<Spark::UI::IconManagerInterface>::Get();
        ImTextureID folderIcon = ImTextureID_Invalid;
        ImTextureID unloadIcon = ImTextureID_Invalid;
        ImTextureID loadingIcon = ImTextureID_Invalid;
        if (iconMgr && m_folderIconId.IsValid()) {
            folderIcon = iconMgr->RequestIconId(m_folderIconId);
        }
        if (iconMgr && m_unloadIconId.IsValid()) {
            unloadIcon = iconMgr->RequestIconId(m_unloadIconId);
        }
        if (iconMgr && m_loadingIconId.IsValid()) {
            loadingIcon = iconMgr->RequestIconId(m_loadingIconId);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float cellW     = 80;
        const float thumbSize = 56;
        const float cellH     = thumbSize + 4 + ImGui::GetTextLineHeight() + 8;
        const float availableW = ImGui::GetContentRegionAvail().x;
        const int   cols = eastl::max(1, (int)(availableW / cellW));
        const float thumbX = (cellW - thumbSize) * 0.5f;

        const char* filter = m_filterBuf.empty() ? nullptr : m_filterBuf.data();

        auto TruncateName = [&](const eastl::string& name) -> eastl::string {
            float maxW = cellW - 8;
            if (ImGui::CalcTextSize(name.c_str()).x <= maxW) {
                return name;
            }
            eastl::string s = name;
            while (s.size() > 3 && ImGui::CalcTextSize((s + "...").c_str()).x > maxW) {
                s.pop_back();
            }
            return s + "...";
        };

        ImVec2 basePos = ImGui::GetCursorScreenPos();
        int idx = 0;

        auto DrawThumb = [&](const eastl::string& name, const AssetEntry* assetEntry = nullptr) {
            int col = idx % cols;
            int row = idx / cols;
            idx++;

            ImVec2 cellPos(basePos.x + col * cellW, basePos.y + row * cellH);

            // Dummy 注册布局边界，在此之后用 draw list 覆盖绘制
            ImGui::SetCursorScreenPos(cellPos);
            ImGui::Dummy(ImVec2(cellW, cellH));

            ImVec2 thumbMin(cellPos.x + thumbX, cellPos.y + 4);
            ImVec2 thumbMax(thumbMin.x + thumbSize, thumbMin.y + thumbSize);

            if (assetEntry)
            {
                const auto* am = Spark::Service<Spark::Resource::AssetManager>::Get();
                const auto asset = am->FindAsset(assetEntry->id);
                if (asset)
                {
                    if (asset->GetStatus() == Spark::Resource::AssetStatus::NotLoaded)
                    {
                        if (unloadIcon != ImTextureID_Invalid) {
                            dl->AddImage(unloadIcon, thumbMin, thumbMax);
                        }
                    }
                    else if 
                    (
                        asset->GetStatus() == Spark::Resource::AssetStatus::Loading ||
                        asset->GetStatus() == Spark::Resource::AssetStatus::Compiling
                    )
                    {
                        if (loadingIcon != ImTextureID_Invalid) {
                            dl->AddImage(loadingIcon, thumbMin, thumbMax);
                        }
                    }
                    else
                    {
                        if (folderIcon != ImTextureID_Invalid) {
                            dl->AddImage(folderIcon, thumbMin, thumbMax);
                        }
                    }
                }
            }
            else
            {
                if (folderIcon != ImTextureID_Invalid) {
                    dl->AddImage(folderIcon, thumbMin, thumbMax);
                }
            }


            eastl::string label = TruncateName(name);
            ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            ImVec2 textPos(cellPos.x + (cellW - textSize.x) * 0.5f,
                           thumbMax.y + 4);
            dl->AddText(textPos, IM_COL32(200, 200, 200, 255), label.c_str());

            if (assetEntry)
            {
                char dragId[64];
                snprintf(dragId, sizeof(dragId), "##drag_%p", static_cast<const void*>(assetEntry));
                ImGui::SetCursorScreenPos(cellPos);
                ImGui::InvisibleButton(dragId, ImVec2(cellW, cellH));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    OpenAsset(*assetEntry);
                }
                if (ImGui::BeginDragDropSource())
                {
                    if (!m_dragAsset)
                    {
                        const auto* am = Spark::Service<Spark::Resource::AssetManager>::Get();
                        m_dragAsset = am->FindAsset(assetEntry->id);
                    }

                    const Spark::Resource::Asset* rawPtr = m_dragAsset.get();
                    ImGui::SetDragDropPayload("DRAG_ASSET_FILE", &rawPtr, sizeof(rawPtr));
                    ImGui::Image(folderIcon, ImVec2(thumbSize, thumbSize));
                    ImGui::Text("%s", name.c_str());
                    ImGui::EndDragDropSource();
                }
                else
                {
                    if (m_dragAsset)
                    {
                        m_dragAsset = nullptr;
                    }
                }
            }
        };

        if (m_selectedAsset != nullptr) {
            DrawThumb(GetAssetDisplayName(m_selectedAsset->id), m_selectedAsset);
        } else {
            for (const auto& folder : m_selectedFolder->children) {
                DrawThumb(folder.name);
            }
            for (const auto& asset : m_selectedFolder->assets) {
                eastl::string assetName = GetAssetDisplayName(asset.id);
                if (filter && filter[0] != '\0') {
                    if (assetName.find(filter) == eastl::string::npos) {
                        continue;
                    }
                }
                DrawThumb(assetName, &asset);
            }
        }

        if (idx == 0) {
            ImGui::TextUnformatted("(empty)");
        }

        ImGui::EndChild();
    }

    const BottomPanel::AssetFolder* BottomPanel::FindFolder(const AssetFolder& folder,
                                                            const eastl::string& fullPath) const
    {
        if (folder.fullPath == fullPath)
        {
            return &folder;
        }
        for (const AssetFolder& child : folder.children)
        {
            if (const AssetFolder* found = FindFolder(child, fullPath))
            {
                return found;
            }
        }
        return nullptr;
    }

    void BottomPanel::FindAsset(const AssetFolder& folder, const Spark::Resource::AssetId& id,
                                const AssetFolder*& outFolder, const AssetEntry*& outEntry) const
    {
        for (const AssetEntry& entry : folder.assets)
        {
            if (entry.id == id)
            {
                outFolder = &folder;
                outEntry  = &entry;
                return;
            }
        }
        for (const AssetFolder& child : folder.children)
        {
            FindAsset(child, id, outFolder, outEntry);
            if (outEntry)
            {
                return;
            }
        }
    }

    void BottomPanel::RebuildTree()
    {
        m_treeBuilt = true;
        LoadIcons();

        // Read and cleared before the vectors they point into are destroyed.
        const eastl::string     keepFolder = m_selectedFolder ? m_selectedFolder->fullPath
                                                              : eastl::string();
        const Spark::Resource::AssetId keepAsset = m_selectedAsset ? m_selectedAsset->id
                                                                   : Spark::Resource::AssetId();
        m_selectedFolder = nullptr;
        m_selectedAsset  = nullptr;
        m_rootFolder     = AssetFolder{};

        // ScanDirectory only lists what the database knows.
        if (auto* assetManager = Spark::Service<Spark::Resource::AssetManager>::Get())
        {
            assetManager->AssetRegistry();
        }

        m_rootFolder.name = "Assets";

        if (auto* fileSystem = Spark::Service<Spark::FileSystem>::Get()) {
            for (const auto& mount : fileSystem->GetMountNames()) {
                if (mount == "cache")
                {
                    continue;
                }
                AssetFolder mountRoot;
                mountRoot.name = mount;
                mountRoot.fullPath = mount + "://";
                ScanDirectory(mountRoot.fullPath, mountRoot);
                m_rootFolder.children.push_back(eastl::move(mountRoot));
            }
        }

        if (keepAsset.IsValid())
        {
            const AssetFolder* owner = nullptr;
            FindAsset(m_rootFolder, keepAsset, owner, m_selectedAsset);
        }
        if (!m_selectedAsset && !keepFolder.empty())
        {
            m_selectedFolder = FindFolder(m_rootFolder, keepFolder);
        }
        if (!m_selectedAsset && !m_selectedFolder)
        {
            m_selectedFolder = &m_rootFolder;
        }
    }

    void BottomPanel::OpenAsset(const AssetEntry& entry)
    {
        using namespace Spark;

        if (entry.type != Resource::AssetType::Material)
        {
            return;
        }

        auto* context = Material::MaterialExecuteContext::Current();
        if (!context)
        {
            return;
        }

        // Resolve, never a fresh entity: it keys on MaterialAssetRef, so this lands on the
        // same material the objects using the asset already render with.
        const Material::MaterialHandle handle = Material::Resolve(*context, entry.id);
        if (handle == Material::NullMaterial)
        {
            LOG_WARN("[BottomPanel] Cannot open material {}", entry.id.GetPath().c_str());
            return;
        }

        MaterialEditBus::Broadcast(&MaterialEditEvents::OpenMaterialEditor, handle);
    }

    void BottomPanel::DrawAssets()
    {
        if (!m_treeBuilt) {
            RebuildTree();
        }

        // ---- 顶部工具栏 ----
        float toolbarHeight = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("AssetToolbar", ImVec2(0, toolbarHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // ---- 搜索框 ----
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26, 4));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(65, 65, 65, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(65, 65, 65, 255));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, IM_COL32(120, 120, 120, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);

        if (m_filterBuf.empty()) {
            m_filterBuf.resize(128, '\0');
        }

        ImVec2 inputPos = ImGui::GetCursorScreenPos();
        float  inputW = 220;
        ImGui::SetNextItemWidth(inputW);
        ImGui::SetNextItemShortcut(ImGuiKey_F | ImGuiMod_Ctrl);
        ImGui::InputTextWithHint("##filter", "Search...", m_filterBuf.data(), m_filterBuf.size());

        // 搜索图标（画在输入框左侧上方）
        ImTextureID searchIcon = ImTextureID_Invalid;
        {
            auto* imgr = Spark::Service<Spark::UI::IconManagerInterface>::Get();
            if (imgr && m_searchIconId.IsValid()) {
                searchIcon = imgr->RequestIconId(m_searchIconId);
            }
        }
        if (searchIcon != ImTextureID_Invalid) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float inputH = ImGui::GetFrameHeight();
            ImVec2 iconPos(inputPos.x + 6, inputPos.y + (inputH - 16) * 0.5f);
            dl->AddImage(searchIcon, iconPos, ImVec2(iconPos.x + 16, iconPos.y + 16));
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);

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
                          ImGuiWindowFlags_None);
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