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

namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    class AssetCache;
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
        Ptr<Asset> LoadAsset(const AssetId& id) override;
        Ptr<Asset> RequestAsset(const AssetId& id) override;
        Ptr<Asset> FindAsset(const AssetId& id) const override;

        AssetType GetSupportAssetType(eastl::string_view file) override;

        AssetId MakeAssetId(eastl::string_view virtualPath) override;

        void AssetRegistry() override;

        void ReleaseAsset(const AssetId& id) override;

        //! Opt-in, main-thread setup of the image builder's GPU EnvironmentBaker.
        //! Call after Init(), before any cubemap asset is requested
        //! (see ImageAssetBuilder::InitEnvironmentBaker). 
        bool InitEnvironmentBaker();

    private:
        Ptr<Asset> CreateAsset(const AssetId& id);

        void EnqueueForProcessing(Asset& asset);

        void ProcessAsset(Asset& asset);

        void ProcessThread();

        AssetId MakeAssetIdForType(eastl::string_view virtualPath, AssetType type);

        mutable std::mutex      m_queueMutex;     ///< 保护 pendingQueue
        std::condition_variable m_cv;
        bool                    m_shutdown{false};
        std::thread             m_processThread;
        eastl::queue<Asset*>    m_pendingQueue;

        //! Resolved once at Init. Its own lock covers concurrent use from the worker thread.
        const FileSystem* m_fileSystem{nullptr};

        //! Stateless past construction, so ProcessAsset uses it from either thread.
        UniquePtr<AssetCache> m_cache;

        SystemUniquePtr<AssetDataBase>       m_db;

        SystemUniquePtr<ImageAssetBuilder>   m_imageBuilder;
        SystemUniquePtr<ShaderAssetBuilder>  m_shaderBuilder;
        SystemUniquePtr<ModelAssetBuilder>   m_modelBuilder;
    };
}
