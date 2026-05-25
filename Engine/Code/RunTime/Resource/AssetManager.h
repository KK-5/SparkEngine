#pragma once

#include <EASTL/vector.h>
#include <EASTL/queue.h>
#include <EASTL/string.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <Service/Service.h>
#include "Asset.h"
#include "AssetManagerInterface.h"

namespace Spark::Resource
{
    class AssetDataBase;
    class ImageAssetBuilder;
    class ShaderAssetBuilder;
    class ModelAssetBuilder;

    class SparkAssetManager final : public Service<AssetManager>::Handler
    {
    public:
        SparkAssetManager();
        ~SparkAssetManager() override;

        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;
        eastl::vector<HashString> Request() const override;
        HashString GetName() const override;

        // AssetManager
        Ptr<Asset> LoadAsset(const AssetId& id, AssetType type) override;
        Ptr<Asset> RequestAsset(const AssetId& id, AssetType type) override;
        Ptr<Asset> FindAsset(const AssetId& id) const override;

        void AddSearchPath(eastl::string_view path) override;
        void RemoveSearchPath(eastl::string_view path) override;

        void ReleaseAsset(const AssetId& id) override;

    private:
        Ptr<Asset> CreateAsset(const AssetId& id, AssetType type);

        void EnqueueForProcessing(Asset& asset);

        void ProcessAsset(Asset& asset);

        void ProcessThread();

        /// 拷贝当前搜索路径，避免 worker 线程长持锁
        eastl::vector<eastl::string> SnapshotSearchPaths() const;

        mutable std::mutex      m_queueMutex;     ///< 保护 pendingQueue
        std::condition_variable m_cv;
        bool                    m_shutdown{false};
        std::thread             m_processThread;
        eastl::queue<Asset*>    m_pendingQueue;

        mutable std::mutex            m_searchPathsMutex;
        eastl::vector<eastl::string>  m_searchPaths;

        SystemUniquePtr<AssetDataBase>       m_db;

        SystemUniquePtr<ImageAssetBuilder>   m_imageBuilder;
        SystemUniquePtr<ShaderAssetBuilder>  m_shaderBuilder;
        SystemUniquePtr<ModelAssetBuilder>   m_modelBuilder;
    };
}
