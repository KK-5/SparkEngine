#pragma once

#include <EASTL/vector.h>
#include <EASTL/deque.h>
#include <EASTL/queue.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <Service/Service.h>
#include "Asset.h"
#include "AssetManagerInterface.h"

namespace Spark::Resource
{
    class SparkAssetManager final : public Service<AssetManager>::Handler
    {
    public:
        // ISystem
        void Initialize() override;
        void Shutdown() override;
        eastl::vector<HashString> Request() const override;
        HashString GetName() const override;

        // AssetManager
        Ptr<Asset> FindAsset(const AssetId& id, AssetType type) const override;
        Ptr<Asset> LoadAsset(const AssetId& id, AssetType type) override;
        Ptr<Asset> RequestAsset(const AssetId& id, AssetType type) override;

        void AddSearchPath(eastl::string_view path) override;
        void RemoveSearchPath(eastl::string_view path) override;

        void ReleaseAsset(const AssetId& id) override;

        void RegisterAssetLoader(eastl::unique_ptr<AssetLoader> loader, AssetType type) override;
        void RegisterAssetCompiler(eastl::unique_ptr<AssetCompiler> compiler, AssetType type) override;

    private:
        /// 在搜索路径中查找资产文件，返回完整路径；找不到则返回空字符串
        /// eastl::string ResolvePath(const AssetId& id) const;

        void ProcessThread();

        void ProcessAsset(Asset& asset);

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_shutdown{false};

        std::thread m_processThread;
        eastl::queue<Asset*> m_pendingQueue;

        eastl::unordered_map<AssetId, Asset*> m_assets;
        eastl::unordered_map<AssetType, eastl::unique_ptr<AssetLoader>> m_assetLoaders;
        eastl::unordered_map<AssetType, eastl::unique_ptr<AssetCompiler>> m_assetCompilers;
        eastl::vector<eastl::string> m_searchPaths;
    };
}
