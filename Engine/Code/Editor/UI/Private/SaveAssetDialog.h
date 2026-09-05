#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <imgui.h>

#include <Resource/AssetTypes.h>

#include "UI/Bus/SaveAssetDialogBus.h"

namespace Editor
{
    //! Names a file that does not exist yet, somewhere under a mount.
    //!
    //! The editor's own picker rather than the system dialog: AssetId::m_path is always
    //! `mount://relative`, and a native one hands back physical paths the user is free to
    //! choose outside every mount -- a control that can give an illegal answer, plus
    //! validation to push that answer back. Here the failure mode does not exist.
    //!
    //! Modal, unlike the material window: picking a path is an action with an end, and
    //! nothing behind it needs watching while it runs.
    class SaveAssetDialog final : public SaveAssetDialogBus::Handler
    {
    public:
        SaveAssetDialog();
        ~SaveAssetDialog() override;

        void Draw();

        // SaveAssetDialogBus
        void OpenSaveAssetDialog(const SaveAssetRequest& request) override;

    private:
        //! One row of the left pane. Flattened rather than nested: the tree is drawn as a
        //! list of rows with an indent, and every operation on it is a scan.
        struct Directory
        {
            eastl::string m_path;             ///< virtual
            eastl::string m_name;             ///< last segment; the mount name at the root
            int           m_depth       = 0;
            bool          m_hasChildren = false;
        };

        void ScanTree();
        void ScanInto(const eastl::string& virtualDir, int depth);
        void ReadCurrentDirectory();
        void SetCurrentDirectory(const eastl::string& virtualDir);

        void DrawTitleBar(float width);
        void DrawPathRow(float width);
        void DrawTree(const ImVec2& size);
        void DrawFileList(const ImVec2& size);
        void DrawNameRow(float width);
        void DrawFooter(float width);

        void Confirm();
        void Close();

        bool          IsExpanded(const eastl::string& path) const;
        void          ToggleExpanded(const eastl::string& path);
        bool          IsVisible(const Directory& directory) const;
        eastl::string FileName() const;   ///< the name field plus the extension
        eastl::string FullPath() const;
        bool          NameIsTaken() const;
        bool          CanSave() const;

        SaveAssetRequest m_request;

        //! Open covers the frames the popup is up; the flag is what makes the one
        //! ImGui::OpenPopup call happen inside Draw, where the id stack is the popup's own.
        bool m_open      = false;
        bool m_openPopup = false;
        bool m_focusName = false;

        //! The last Save was refused or failed. Cleared by anything that changes what
        //! would be written, since that is what makes trying again worthwhile.
        bool m_saveFailed = false;

        eastl::vector<Directory>     m_tree;
        eastl::vector<eastl::string> m_expanded;
        eastl::vector<eastl::string> m_files;   ///< names in the current directory
        eastl::string                m_currentDir;
        eastl::vector<char>          m_nameBuf;

        Spark::Resource::AssetId m_folderIconId;
        bool                     m_iconLoaded = false;
    };
}
