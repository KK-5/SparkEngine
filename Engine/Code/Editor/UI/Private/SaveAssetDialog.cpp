#include "SaveAssetDialog.h"

#include <cstring>

#include <EASTL/algorithm.h>

#include <Service/Service.h>
#include <VFS/FileSystem.h>
#include <Feature/UI/ImGui/IconManagerInterface.h>

#include "EditorTheme.h"

namespace Editor
{
    using namespace Spark;

    namespace
    {
        constexpr const char* kPopupId = "SaveAssetDialog";

        //! Only the project mount. The engine's and the editor's own assets ship with the
        //! build; a place to save user content is not what they are.
        constexpr const char* kRoot = "project://";

        // Mockup pixels, put on this screen -- see MaterialWindow for why every length
        // goes through Theme::Px.
        constexpr float kWindowW      = Theme::Px(600.f);
        constexpr float kWindowH      = Theme::Px(472.f);
        constexpr float kTitleHeight  = Theme::Px(34.f);
        constexpr float kPathHeight   = Theme::Px(31.f);
        constexpr float kNameHeight   = Theme::Px(73.f);
        constexpr float kFooterHeight = Theme::Px(41.f);
        constexpr float kTreeWidth    = Theme::Px(238.f);
        constexpr float kHeaderHeight = Theme::Px(21.f);
        constexpr float kRowHeight    = Theme::Px(22.f);
        constexpr float kIndent       = Theme::Px(15.f);
        constexpr float kPad          = Theme::Px(12.f);
        constexpr float kCloseSize    = Theme::Px(24.f);
        constexpr float kIconSize     = Theme::Px(12.f);

        //! A selected file row. The tree's own selection is the accent wash below.
        constexpr ImU32 kRowSelected = IM_COL32(0x1E, 0x23, 0x28, 0xFF);
        constexpr ImU32 kTreeSelected = IM_COL32(0x7F, 0xD6, 0xC2, 0x1F);

        constexpr size_t kNameCapacity = 128;

        void TintedText(ImU32 color, const char* text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        }

        //! The faint mono header a pane opens with.
        void PaneLabel(const char* text)
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeHeader);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Theme::Px(6.f));
            ImGui::Indent(kPad);
            TintedText(Theme::kTextFaint, text);
            ImGui::Unindent(kPad);
        }

        //! Identifiers only, and the first character cannot be a digit. Deliberately
        //! stricter than the filesystem: a name that reads the same in a path, in a log
        //! line and in generated code is one less thing to encode later.
        bool NameIsValid(const char* name)
        {
            const auto letter = [](char c)
            {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
            };
            const auto digit = [](char c) { return c >= '0' && c <= '9'; };

            if (!name || name[0] == '\0' || !letter(name[0]))
            {
                return false;
            }
            for (const char* p = name + 1; *p != '\0'; ++p)
            {
                if (!letter(*p) && !digit(*p))
                {
                    return false;
                }
            }
            return true;
        }

        eastl::string LastSegment(eastl::string_view path)
        {
            const size_t slash = path.rfind('/');
            const eastl::string_view name =
                (slash == eastl::string_view::npos) ? path : path.substr(slash + 1);
            return eastl::string(name.data(), name.size());
        }

        //! The directory holding `path`, empty when `path` is a mount root. The root keeps
        //! its trailing slash (`project://`), everything below it does not.
        eastl::string ParentDir(const eastl::string& path)
        {
            if (path.empty() || path.back() == '/')
            {
                return {};
            }
            const size_t slash = path.rfind('/');
            if (slash == eastl::string::npos)
            {
                return {};
            }
            if (slash > 0 && path[slash - 1] == '/')
            {
                return path.substr(0, slash + 1);
            }
            return path.substr(0, slash);
        }

        eastl::string JoinDir(const eastl::string& dir, const eastl::string& name)
        {
            eastl::string out = dir;
            if (!out.empty() && out.back() != '/')
            {
                out += '/';
            }
            out += name;
            return out;
        }

        bool EndsWith(eastl::string_view text, eastl::string_view suffix)
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(),
                                suffix.data(), suffix.size()) == 0;
        }
    }

    SaveAssetDialog::SaveAssetDialog()
    {
        SaveAssetDialogBus::Handler::BusConnect();
        m_nameBuf.resize(kNameCapacity, '\0');
    }

    SaveAssetDialog::~SaveAssetDialog()
    {
        if (BusIsConnected())
        {
            BusDisconnect();
        }
    }

    void SaveAssetDialog::OpenSaveAssetDialog(const SaveAssetRequest& request)
    {
        m_request    = request;
        m_open       = true;
        m_openPopup  = true;
        m_focusName  = true;

        m_nameBuf.assign(kNameCapacity, '\0');
        const size_t length = eastl::min(request.m_defaultName.size(), kNameCapacity - 1);
        memcpy(m_nameBuf.data(), request.m_defaultName.data(), length);

        // Once per opening, not per frame: the tree is a picture of the disk taken now,
        // and nothing can change it while a modal is up.
        ScanTree();

        const bool known = !request.m_defaultDir.empty()
                        && eastl::find_if(m_tree.begin(), m_tree.end(),
                                          [&](const Directory& d)
                                          { return d.m_path == request.m_defaultDir; })
                           != m_tree.end();
        SetCurrentDirectory(known ? request.m_defaultDir : eastl::string(kRoot));
    }

    void SaveAssetDialog::ScanTree()
    {
        m_tree.clear();
        m_expanded.clear();

        auto* fileSystem = Service<FileSystem>::Get();
        if (!fileSystem)
        {
            return;
        }

        Directory root;
        root.m_path = kRoot;
        root.m_name = "project";
        m_tree.push_back(root);

        ScanInto(root.m_path, 1);
        m_tree.front().m_hasChildren = m_tree.size() > 1;
        m_expanded.push_back(kRoot);
    }

    void SaveAssetDialog::ScanInto(const eastl::string& virtualDir, int depth)
    {
        auto* fileSystem = Service<FileSystem>::Get();
        if (!fileSystem)
        {
            return;
        }

        fileSystem->ListDirectory(virtualDir, [&](eastl::string_view path, bool isDirectory)
        {
            if (!isDirectory)
            {
                return;
            }

            Directory entry;
            entry.m_path  = eastl::string(path.data(), path.size());
            entry.m_name  = LastSegment(path);
            entry.m_depth = depth;

            // The child's path by value: the recursion pushes onto m_tree, and a reference
            // into it would not survive the reallocation.
            const eastl::string childPath = entry.m_path;

            const size_t self   = m_tree.size();
            m_tree.push_back(eastl::move(entry));

            const size_t before = m_tree.size();
            ScanInto(childPath, depth + 1);
            m_tree[self].m_hasChildren = m_tree.size() > before;
        });
    }

    void SaveAssetDialog::ReadCurrentDirectory()
    {
        m_files.clear();

        auto* fileSystem = Service<FileSystem>::Get();
        if (!fileSystem || m_currentDir.empty())
        {
            return;
        }

        fileSystem->ListDirectory(m_currentDir, [&](eastl::string_view path, bool isDirectory)
        {
            if (!isDirectory && EndsWith(path, m_request.m_extension))
            {
                m_files.push_back(LastSegment(path));
            }
        });
    }

    void SaveAssetDialog::SetCurrentDirectory(const eastl::string& virtualDir)
    {
        m_currentDir = virtualDir;
        ReadCurrentDirectory();

        // Open the way down to it, or the row the dialog starts on is not on screen.
        for (eastl::string dir = ParentDir(virtualDir); !dir.empty(); dir = ParentDir(dir))
        {
            if (!IsExpanded(dir))
            {
                m_expanded.push_back(dir);
            }
        }
    }

    bool SaveAssetDialog::IsExpanded(const eastl::string& path) const
    {
        return eastl::find(m_expanded.begin(), m_expanded.end(), path) != m_expanded.end();
    }

    void SaveAssetDialog::ToggleExpanded(const eastl::string& path)
    {
        auto it = eastl::find(m_expanded.begin(), m_expanded.end(), path);
        if (it == m_expanded.end())
        {
            m_expanded.push_back(path);
        }
        else
        {
            m_expanded.erase(it);
        }
    }

    bool SaveAssetDialog::IsVisible(const Directory& directory) const
    {
        for (eastl::string dir = ParentDir(directory.m_path); !dir.empty(); dir = ParentDir(dir))
        {
            if (!IsExpanded(dir))
            {
                return false;
            }
        }
        return true;
    }

    eastl::string SaveAssetDialog::FileName() const
    {
        eastl::string name = m_nameBuf.data();
        name += m_request.m_extension;
        return name;
    }

    eastl::string SaveAssetDialog::FullPath() const
    {
        return JoinDir(m_currentDir, FileName());
    }

    bool SaveAssetDialog::NameIsTaken() const
    {
        const eastl::string name = FileName();
        return eastl::find(m_files.begin(), m_files.end(), name) != m_files.end();
    }

    void SaveAssetDialog::Confirm()
    {
        const eastl::string path = FullPath();
        Close();
        SaveAssetDialogBus::Broadcast(&SaveAssetDialogEvents::OnSaveAssetConfirmed, path);
    }

    void SaveAssetDialog::Close()
    {
        m_open = false;
        ImGui::CloseCurrentPopup();
    }

    void SaveAssetDialog::Draw()
    {
        if (!m_open)
        {
            return;
        }

        if (!m_iconLoaded)
        {
            m_iconLoaded = true;
            if (auto* icons = Service<UI::IconManagerInterface>::Get())
            {
                m_folderIconId = icons->OpenIcon("editor://folder.svg");
            }
        }

        // Before Begin: window background, padding and rounding are read there. PopupBg is
        // the theme's raised block, and this window is not one.
        Theme::Scoped theme;
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::kWindowBg);

        if (m_openPopup)
        {
            ImGui::OpenPopup(kPopupId);
            m_openPopup = false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(kWindowW, kWindowH), ImGuiCond_Appearing);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                                     | ImGuiWindowFlags_NoResize
                                     | ImGuiWindowFlags_NoMove
                                     | ImGuiWindowFlags_NoScrollbar
                                     | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::BeginPopupModal(kPopupId, nullptr, flags))
        {
            Theme::ScopedFont body(Theme::Face::UI, Theme::kSizeBody);

            const float width = ImGui::GetContentRegionAvail().x;

            DrawTitleBar(width);
            DrawPathRow(width);

            const float bodyHeight = ImGui::GetContentRegionAvail().y - kNameHeight - kFooterHeight;
            if (bodyHeight > 0.f)
            {
                const ImVec2 origin = ImGui::GetCursorScreenPos();

                DrawTree(ImVec2(kTreeWidth, bodyHeight));

                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(origin.x + kTreeWidth + 0.5f, origin.y),
                    ImVec2(origin.x + kTreeWidth + 0.5f, origin.y + bodyHeight),
                    Theme::kBorderPanel);

                ImGui::SetCursorScreenPos(ImVec2(origin.x + kTreeWidth + 1.f, origin.y));
                DrawFileList(ImVec2(width - kTreeWidth - 1.f, bodyHeight));

                ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + bodyHeight));
            }

            DrawNameRow(width);
            DrawFooter(width);

            // Enter outside the name field. Inside it the field reports the key itself,
            // since it swallows what it has focus on.
            if (m_open && !ImGui::IsAnyItemActive()
                && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
                && NameIsValid(m_nameBuf.data()) && !NameIsTaken())
            {
                Confirm();
            }

            ImGui::EndPopup();
        }
        else
        {
            // Escape closes a modal without asking us; this is where that becomes a cancel.
            m_open = false;
        }

        ImGui::PopStyleColor();
    }

    void SaveAssetDialog::DrawTitleBar(float width)
    {
        ImDrawList*  draw = ImGui::GetWindowDrawList();
        const ImVec2 p0   = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + width, p0.y + kTitleHeight);

        draw->AddRectFilled(p0, p1, Theme::kTitleBg, ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersTop);
        draw->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), Theme::kBorderWindow);

        {
            Theme::ScopedFont font(Theme::Face::Bold, Theme::kSizeTitle);
            draw->AddText(ImVec2(p0.x + kPad, p0.y + (kTitleHeight - ImGui::GetTextLineHeight()) * 0.5f),
                          Theme::kTextStrong, m_request.m_title.c_str());
        }

        const float closeMargin = Theme::Px(6.f);
        ImGui::SetCursorScreenPos(
            ImVec2(p1.x - closeMargin - kCloseSize, p0.y + (kTitleHeight - kCloseSize) * 0.5f));
        if (ImGui::InvisibleButton("##Close", ImVec2(kCloseSize, kCloseSize)))
        {
            Close();
        }
        {
            const bool   hovered = ImGui::IsItemHovered();
            const ImVec2 min     = ImGui::GetItemRectMin();
            const ImVec2 max     = ImGui::GetItemRectMax();
            if (hovered)
            {
                draw->AddRectFilled(min, max, Theme::kCloseHovBg, Theme::Px(3.f));
            }

            const ImU32 color = hovered ? Theme::kCloseHovText : Theme::kTextDim;
            const float inset = Theme::Px(7.f);
            draw->AddLine(ImVec2(min.x + inset, min.y + inset),
                          ImVec2(max.x - inset, max.y - inset), color, 1.5f);
            draw->AddLine(ImVec2(max.x - inset, min.y + inset),
                          ImVec2(min.x + inset, max.y - inset), color, 1.5f);
        }

        ImGui::SetCursorScreenPos(p0);
        ImGui::Dummy(ImVec2(width, kTitleHeight));
    }

    void SaveAssetDialog::DrawPathRow(float width)
    {
        ImDrawList*  draw = ImGui::GetWindowDrawList();
        const ImVec2 p0   = ImGui::GetCursorScreenPos();

        draw->AddLine(ImVec2(p0.x, p0.y + kPathHeight), ImVec2(p0.x + width, p0.y + kPathHeight),
                      Theme::kBorderInner);

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            const float       y = p0.y + (kPathHeight - ImGui::GetTextLineHeight()) * 0.5f;

            const char* label = "Project root";
            draw->AddText(ImVec2(p0.x + kPad, y), Theme::kTextFaint, label);

            const float x = p0.x + kPad + ImGui::CalcTextSize(label).x + Theme::Px(8.f);
            draw->AddText(ImVec2(x, y), Theme::kTextDim, m_currentDir.c_str());
        }

        ImGui::Dummy(ImVec2(width, kPathHeight));
    }

    void SaveAssetDialog::DrawTree(const ImVec2& size)
    {
        ImGui::BeginChild("##TreePane", size, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        PaneLabel("DIRECTORIES");

        ImGui::BeginChild("##TreeRows", ImVec2(0.f, ImGui::GetContentRegionAvail().y), false);

        ImTextureID folder = ImTextureID_Invalid;
        if (auto* icons = Service<UI::IconManagerInterface>::Get(); icons && m_folderIconId.IsValid())
        {
            folder = icons->RequestIconId(m_folderIconId);
        }

        Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);

        for (const Directory& directory: m_tree)
        {
            if (!IsVisible(directory))
            {
                continue;
            }

            ImGui::PushID(directory.m_path.c_str());

            const float indent = Theme::Px(5.f) + directory.m_depth * kIndent;
            ImGui::SetCursorPosX(indent);

            // The caret is its own item so that hitting it toggles without also moving
            // the selection -- one row, two answers.
            const float caret = Theme::Px(11.f);
            ImGui::InvisibleButton("##Caret", ImVec2(caret, kRowHeight));
            if (directory.m_hasChildren)
            {
                if (ImGui::IsItemClicked())
                {
                    ToggleExpanded(directory.m_path);
                }

                const ImVec2 min  = ImGui::GetItemRectMin();
                const float  half = kRowHeight * 0.5f;
                const float  arm  = Theme::Px(3.5f);
                const ImU32  tint = ImGui::IsItemHovered() ? Theme::kTextLabel : Theme::kTextDimmer;

                if (IsExpanded(directory.m_path))
                {
                    ImGui::GetWindowDrawList()->AddTriangleFilled(
                        ImVec2(min.x, min.y + half - arm), ImVec2(min.x + arm * 2.f, min.y + half - arm),
                        ImVec2(min.x + arm, min.y + half + arm), tint);
                }
                else
                {
                    ImGui::GetWindowDrawList()->AddTriangleFilled(
                        ImVec2(min.x, min.y + half - arm), ImVec2(min.x + arm * 2.f, min.y + half),
                        ImVec2(min.x, min.y + half + arm), tint);
                }
            }

            ImGui::SameLine(0.f, Theme::Px(4.f));

            const bool selected = directory.m_path == m_currentDir;

            ImGui::PushStyleColor(ImGuiCol_Header, kTreeSelected);
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? Theme::kTextStrong : Theme::kTextLabel);
            if (ImGui::Selectable("##Row", selected, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x, kRowHeight)))
            {
                SetCurrentDirectory(directory.m_path);
            }

            const ImVec2 rowMin = ImGui::GetItemRectMin();
            float        textX  = rowMin.x + Theme::Px(3.f);

            if (folder != ImTextureID_Invalid)
            {
                const float iconY = rowMin.y + (kRowHeight - kIconSize) * 0.5f;
                ImGui::GetWindowDrawList()->AddImage(
                    folder, ImVec2(textX, iconY), ImVec2(textX + kIconSize, iconY + kIconSize));
                textX += kIconSize + Theme::Px(6.f);
            }

            ImGui::GetWindowDrawList()->AddText(
                ImVec2(textX, rowMin.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                selected ? Theme::kTextStrong : Theme::kTextLabel, directory.m_name.c_str());

            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::EndChild();
    }

    void SaveAssetDialog::DrawFileList(const ImVec2& size)
    {
        ImGui::BeginChild("##FilePane", size, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        PaneLabel("CONTENTS");

        ImGui::BeginChild("##FileRows", ImVec2(0.f, ImGui::GetContentRegionAvail().y), false);

        Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);

        const eastl::string current = FileName();

        for (const eastl::string& file: m_files)
        {
            ImGui::PushID(file.c_str());
            ImGui::SetCursorPosX(Theme::Px(7.f));

            const bool selected = file == current;

            ImGui::PushStyleColor(ImGuiCol_Header, kRowSelected);
            if (ImGui::Selectable("##File", selected, 0,
                                  ImVec2(ImGui::GetContentRegionAvail().x - Theme::Px(7.f), kRowHeight)))
            {
                // Fills the name field: picking an existing file is how a user says "this
                // one", and the clash message then explains why Save is off.
                const eastl::string stem = file.substr(0, file.size() - m_request.m_extension.size());
                m_nameBuf.assign(kNameCapacity, '\0');
                memcpy(m_nameBuf.data(), stem.data(),
                       eastl::min(stem.size(), kNameCapacity - 1));
            }

            const ImVec2 rowMin = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(rowMin.x + Theme::Px(6.f),
                       rowMin.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                selected ? Theme::kTextStrong : Theme::kTextDim, file.c_str());

            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::EndChild();
    }

    void SaveAssetDialog::DrawNameRow(float width)
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(p0, ImVec2(p0.x + width, p0.y), Theme::kBorderPanel);

        ImGui::BeginChild("##NameRow", ImVec2(width, kNameHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                              | ImGuiWindowFlags_AlwaysUseWindowPadding);

        ImGui::SetCursorPos(ImVec2(kPad, Theme::Px(10.f)));

        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);
            ImGui::AlignTextToFramePadding();
            TintedText(Theme::kTextDim, "Name");
        }
        ImGui::SameLine(0.f, Theme::Px(9.f));

        const float extensionWidth = [&]
        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            return ImGui::CalcTextSize(m_request.m_extension.c_str()).x;
        }();

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);

            if (m_focusName)
            {
                ImGui::SetKeyboardFocusHere();
                m_focusName = false;
            }

            ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX() - extensionWidth
                                    - kPad - Theme::Px(9.f));
            if (ImGui::InputText("##Name", m_nameBuf.data(), m_nameBuf.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue)
                && NameIsValid(m_nameBuf.data()) && !NameIsTaken())
            {
                Confirm();
            }

            ImGui::SameLine(0.f, Theme::Px(9.f));
            TintedText(Theme::kTextFaint, m_request.m_extension.c_str());
        }

        // The message line keeps its height whether or not it says anything, so the row
        // below does not move as the user types. The clash message is where the overwrite
        // checkbox would go if overwriting is ever allowed.
        ImGui::SetCursorPos(ImVec2(kPad, ImGui::GetCursorPosY() + Theme::Px(7.f)));
        {
            Theme::ScopedFont font(Theme::Face::UI, Theme::kSizeLabel);
            if (!NameIsValid(m_nameBuf.data()))
            {
                TintedText(Theme::kCloseHovText,
                           "A name may hold letters, digits and underscores, and cannot "
                           "start with a digit.");
            }
            else if (NameIsTaken())
            {
                TintedText(Theme::kDirty, "An asset of this name is already here.");
            }
        }

        ImGui::EndChild();
    }

    void SaveAssetDialog::DrawFooter(float width)
    {
        ImDrawList*  draw = ImGui::GetWindowDrawList();
        const ImVec2 p0   = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + width, p0.y + kFooterHeight);

        draw->AddRectFilled(p0, p1, Theme::kFooterBg, ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersBottom);
        draw->AddLine(p0, ImVec2(p1.x, p0.y), Theme::kBorderPanel);

        {
            Theme::ScopedFont font(Theme::Face::Mono, Theme::kSizeMono);
            draw->AddText(ImVec2(p0.x + kPad, p0.y + (kFooterHeight - ImGui::GetTextLineHeight()) * 0.5f),
                          Theme::kTextDimmer, FullPath().c_str());
        }

        const bool canSave = NameIsValid(m_nameBuf.data()) && !NameIsTaken();

        struct FooterButton
        {
            const char* label;
            bool        accent;
            bool        enabled;
        };
        const FooterButton buttons[] = {{"Save", true, canSave}, {"Cancel", false, true}};

        float x = p1.x - kPad;
        for (const FooterButton& button: buttons)
        {
            Theme::ScopedFont font(button.accent ? Theme::Face::Bold : Theme::Face::UI,
                                   Theme::kSizeBody);

            const float extra       = button.accent ? Theme::Px(4.f) : 0.f;
            const float buttonWidth = ImGui::CalcTextSize(button.label).x
                                    + ImGui::GetStyle().FramePadding.x * 2.f + extra;
            x -= buttonWidth;

            ImGui::BeginDisabled(!button.enabled);

            ImGui::SetCursorScreenPos(
                ImVec2(x, p0.y + (kFooterHeight - ImGui::GetFrameHeight()) * 0.5f));
            if (button.accent)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHov);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccent);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kOnAccent);
                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextLabel);
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorderWindow);
            }

            if (ImGui::Button(button.label, ImVec2(buttonWidth, 0.f)))
            {
                if (button.accent)
                {
                    Confirm();
                }
                else
                {
                    Close();
                }
            }

            ImGui::PopStyleColor(5);
            ImGui::EndDisabled();

            x -= Theme::Px(9.f);
        }

        ImGui::SetCursorScreenPos(p0);
        ImGui::Dummy(ImVec2(width, kFooterHeight));
    }
}
