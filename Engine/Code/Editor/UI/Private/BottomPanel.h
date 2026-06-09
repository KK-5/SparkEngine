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
        struct AssetEntry
        {
            Spark::Resource::AssetId   id;
            Spark::Resource::AssetType type = Spark::Resource::AssetType::Unknown;
        };

        struct AssetFolder
        {
            eastl::string              name;
            eastl::string              fullPath;
            eastl::vector<AssetFolder> children;
            eastl::vector<AssetEntry>  assets;
        };

        void ScanDirectory(const eastl::string& path, AssetFolder& folder);
        void DrawFolderTree(const AssetFolder& folder);
        void DrawAssetList();

        AssetFolder          m_rootFolder;
        const AssetFolder*   m_selectedFolder = nullptr;
        const AssetEntry*    m_selectedAsset = nullptr;
        eastl::vector<char>  m_filterBuf{};
        bool                 m_treeBuilt = false;

        Spark::ConstPtr<Spark::Resource::Asset>   m_dragAsset = nullptr;

        Tab currentTab = Tab::CONSILE;

        // Icon resources
        bool                     m_iconsLoaded = false;
        Spark::Resource::AssetId m_folderIconId;
        Spark::Resource::AssetId m_fileIconId;
        Spark::Resource::AssetId m_consoleIconId;
        Spark::Resource::AssetId m_assetsIconId;
        Spark::Resource::AssetId m_searchIconId;
        Spark::Resource::AssetId m_unloadIconId;
        Spark::Resource::AssetId m_loadingIconId;
    };
}