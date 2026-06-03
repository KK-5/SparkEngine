#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Resource/Asset.h>

namespace Editor
{
    class BottomPanel final
    {
    public:
        void Draw();

        enum class Tab
        {
            CONSILE,
            ASSETS
        };

    private:
        void DrawConsole();
        void DrawAssets();

        void LoadIcons();

        // ---- asset browser ----
        struct FileEntry
        {
            eastl::string name;
            eastl::string fullPath;
            bool         isDirectory = false;
        };

        struct AssetFolder
        {
            eastl::string              name;
            eastl::string              fullPath;
            eastl::vector<AssetFolder> children;
            eastl::vector<FileEntry>   files;
        };

        void ScanDirectory(const eastl::string& path, AssetFolder& folder);
        void DrawFolderTree(const AssetFolder& folder);
        void DrawAssetList();

        eastl::string        m_searchRoot;       // root directory of the project content
        AssetFolder          m_rootFolder;
        const AssetFolder*   m_selectedFolder = nullptr;
        eastl::vector<char>  m_filterBuf{};
        bool                 m_treeBuilt = false;

        Tab currentTab = Tab::CONSILE;

        // Icon resources
        bool                   m_iconsLoaded = false;
        Spark::Resource::AssetId m_folderIconId;
        Spark::Resource::AssetId m_fileIconId;
    };
}