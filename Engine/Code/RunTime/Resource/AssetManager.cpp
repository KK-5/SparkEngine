#include "AssetManager.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include "AssetDataBase.h"
#include "AssetBuildContext.h"
#include "EBus/AssetBuildBus.h"
#include "EBus/AssetBus.h"
#include "Image/ImageAssetBuilder.h"
#include "Shader/ShaderAssetBuilder.h"
#include "Model/ModelAssetBuilder.h"

namespace Spark::Resource
{
    SparkAssetManager::SparkAssetManager() = default;
    SparkAssetManager::~SparkAssetManager() = default;

    void SparkAssetManager::InitInternal()
    {
        // 先起 DataBase（内部仓储）
        m_db = CreateSystem<AssetDataBase>();
        m_db->Init();

        // 再起所有内置 Builders（在 Bus 上注册 handler）。新增 AssetType 时在这里加一行。
        m_imageBuilder  = CreateSystem<ImageAssetBuilder>();
        m_imageBuilder->Init();

        m_shaderBuilder = CreateSystem<ShaderAssetBuilder>();
        m_shaderBuilder->Init();

        m_modelBuilder = CreateSystem<ModelAssetBuilder>();
        m_modelBuilder->Init();

        m_shutdown = false;
        m_processThread = std::thread(&SparkAssetManager::ProcessThread, this);
    }

    void SparkAssetManager::ShutdownInternal()
    {
        {
            std::lock_guard lock(m_queueMutex);
            m_shutdown = true;
        }
        m_cv.notify_one();

        if (m_processThread.joinable())
        {
            m_processThread.join();
        }

        // 反向顺序释放：Builders（断开 Bus） → DataBase
        m_modelBuilder.reset();
        m_shaderBuilder.reset();
        m_imageBuilder.reset();

        {
            std::lock_guard lock(m_queueMutex);
            m_pendingQueue.swap(eastl::queue<Asset*>());
        }
        {
            std::lock_guard lock(m_searchPathsMutex);
            m_searchPaths.clear();
        }
        m_db.reset();
    }

    eastl::vector<HashString> SparkAssetManager::Request() const
    {
        return {"LogSystem"_hs};
    }

    HashString SparkAssetManager::GetName() const
    {
        return "AssetManager"_hs;
    }

    Ptr<Asset> SparkAssetManager::CreateAsset(const AssetId& id, AssetType type)
    {
        Ptr<Asset> result;
        AssetBuildBus::EventResult(result, type, &AssetBuildEvents::CreateAsset, id);
        if (!result)
        {
            LOG_ERROR("[SparkAssetManager] No builder registered for AssetType {}",
                static_cast<uint32_t>(type));
        }
        return result;
    }

    Ptr<Asset> SparkAssetManager::FindAsset(const AssetId& id) const
    {
        return m_db ? m_db->Find(id) : nullptr;
    }

    Ptr<Asset> SparkAssetManager::LoadAsset(const AssetId& id, AssetType type)
    {
        Ptr<Asset> existing = m_db->Find(id);
        if (existing)
        {
            return existing;
        }

        Ptr<Asset> created = CreateAsset(id, type);
        if (!created)
        {
            return nullptr;
        }

        Ptr<Asset> stored = m_db->InsertOrGet(id, created);
        if (stored.get() != created.get())
        {
            // 竞争失败：别人先注册，直接返回别人的
            return stored;
        }

        // 同步走完处理
        ProcessAsset(*stored);
        return stored;
    }

    Ptr<Asset> SparkAssetManager::RequestAsset(const AssetId& id, AssetType type)
    {
        Ptr<Asset> existing = m_db->Find(id);
        if (existing)
        {
            return existing;
        }

        Ptr<Asset> created = CreateAsset(id, type);
        if (!created)
        {
            return nullptr;
        }

        Ptr<Asset> stored = m_db->InsertOrGet(id, created);
        if (stored.get() != created.get())
        {
            return stored;
        }

        EnqueueForProcessing(*stored);
        return stored;
    }

    void SparkAssetManager::EnqueueForProcessing(Asset& asset)
    {
        asset.SetStatus(AssetStatus::Queued);
        {
            std::lock_guard lock(m_queueMutex);
            m_pendingQueue.push(&asset);
        }
        m_cv.notify_one();
    }

    void SparkAssetManager::AddSearchPath(eastl::string_view path)
    {
        std::lock_guard lock(m_searchPathsMutex);
        m_searchPaths.emplace_back(path.data(), path.size());
    }

    void SparkAssetManager::RemoveSearchPath(eastl::string_view path)
    {
        std::lock_guard lock(m_searchPathsMutex);
        eastl::string pathStr(path.data(), path.size());
        auto it = eastl::find(m_searchPaths.begin(), m_searchPaths.end(), pathStr);
        if (it != m_searchPaths.end())
        {
            m_searchPaths.erase(it);
        }
    }

    void SparkAssetManager::ReleaseAsset(const AssetId& id)
    {
        if (m_db)
        {
            m_db->Remove(id);
        }
    }

    eastl::vector<eastl::string> SparkAssetManager::SnapshotSearchPaths() const
    {
        std::lock_guard lock(m_searchPathsMutex);
        return m_searchPaths;
    }

    void SparkAssetManager::ProcessThread()
    {
        while (true)
        {
            Asset* asset = nullptr;
            {
                std::unique_lock lock(m_queueMutex);
                m_cv.wait(lock, [this] { return m_shutdown || !m_pendingQueue.empty(); });

                if (m_shutdown && m_pendingQueue.empty())
                {
                    return;
                }
                asset = m_pendingQueue.front();
                m_pendingQueue.pop();
            }

            ProcessAsset(*asset);
        }
    }

    void SparkAssetManager::ProcessAsset(Asset& asset)
    {
        AssetBuildContext ctx;
        ctx.id          = asset.GetAssetId();
        ctx.type        = asset.GetAssetType();
        ctx.searchPaths = SnapshotSearchPaths();
        ctx.db          = m_db.get();

        asset.SetStatus(AssetStatus::Loading);
        AssetBuildBus::Event(ctx.type, &AssetBuildEvents::Load, ctx);
        if (!ctx.rawData)
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(ctx.type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        asset.SetStatus(AssetStatus::Compiling);
        AssetBuildBus::Event(ctx.type, &AssetBuildEvents::Compile, ctx);
        if (!ctx.compiledData)
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(ctx.type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        asset.SetDataReady(eastl::move(ctx.compiledData));
        AssetBus::Event(ctx.type, &AssetBus::Events::OnAssetReady, asset);
    }
}
