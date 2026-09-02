#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Resource/Asset.h>
#include <VFS/FileEventBus.h>

namespace Editor
{
    class BottomPanel final : public Spark::FileEventBus::Handler
    {
    public:
        BottomPanel();
        ~BottomPanel() override;

        void Draw();

        // All three only mark the tree stale; the rebuild happens in the next DrawAssets,
        // so none of them depends on running before or after AssetManager's handler.
        void OnFileAdded(eastl::string virtualPath) override;
        void OnFileRemoved(eastl::string virtualPath) override;
        void OnFileWatchOverflow() override;

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

        //! Throws the tree away and walks the mounts again. Selection is carried across by
        //! VALUE: m_selectedFolder and m_selectedAsset point into the vectors being rebuilt.
        void RebuildTree();

        const AssetFolder* FindFolder(const AssetFolder& folder, const eastl::string& fullPath) const;

        void FindAsset(const AssetFolder& folder, const Spark::Resource::AssetId& id,
                       const AssetFolder*& outFolder, const AssetEntry*& outEntry) const;

        //! Double-click. Only materials do anything so far.
        void OpenAsset(const AssetEntry& entry);

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