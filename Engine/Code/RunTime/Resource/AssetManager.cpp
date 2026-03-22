#include "AssetManager.h"

#include <EASTL/algorithm.h>
#include <Log/SpdLogSystem.h>

#include <filesystem>

namespace Spark::Resource
{
    void SparkAssetManager::Initialize()
    {
        m_shutdown = false;
        m_processThread = std::thread(&SparkAssetManager::ProcessThread, this);
        LOG_INFO("SparkAssetManager initialized");
    }

    void SparkAssetManager::Shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            m_shutdown = true;
        }
        m_cv.notify_one();

        if (m_processThread.joinable())
        {
            m_processThread.join();
        }

        m_pendingQueue.swap(eastl::queue<Asset*>());
        m_assets.clear();
        m_assetLoaders.clear();
        m_assetCompilers.clear();
        m_searchPaths.clear();

        LOG_INFO("SparkAssetManager shutdown");
    }

    eastl::vector<HashString> SparkAssetManager::Request() const
    {
        return {"LogSystem"_hs};
    }

    HashString SparkAssetManager::GetName() const
    {
        return "AssetManager"_hs;
    }

    Ptr<Asset> SparkAssetManager::FindAsset(const AssetId& id, AssetType type) const
    {
        std::lock_guard lock(m_mutex);
        auto it = m_assets.find(id);
        if (it != m_assets.end())
        {
            return it->second;
        }
        return nullptr;
    }

    Ptr<Asset> SparkAssetManager::LoadAsset(const AssetId& id, AssetType type)
    {
        // 先查缓存
        {
            std::lock_guard lock(m_mutex);
            auto it = m_assets.find(id);
            if (it != m_assets.end())
            {
                return it->second;
            }
        }

        Ptr<Asset> asset(new Asset(id, type));

        ProcessAsset(*asset);

        {
            std::lock_guard lock(m_mutex);
            m_assets[id] = asset;
        }
        return asset;
    }

    Ptr<Asset> SparkAssetManager::RequestAsset(const AssetId& id, AssetType type)
    {
        {
            std::lock_guard lock(m_mutex);
            auto it = m_assets.find(id);
            if (it != m_assets.end())
            {
                return it->second;
            }

            Ptr<Asset> asset(new Asset(id, type));
            SetAssetStatus(*asset, AssetStatus::Queued);
            m_assets[id] = asset;
            m_pendingQueue.push(asset.get());

            m_cv.notify_one();
            return asset;
        }
    }

    void SparkAssetManager::AddSearchPath(eastl::string_view path)
    {
        std::lock_guard lock(m_mutex);
        m_searchPaths.emplace_back(path.data(), path.size());
    }

    void SparkAssetManager::RemoveSearchPath(eastl::string_view path)
    {
        std::lock_guard lock(m_mutex);
        eastl::string pathStr(path.data(), path.size());
        auto it = eastl::find(m_searchPaths.begin(), m_searchPaths.end(), pathStr);
        if (it != m_searchPaths.end())
        {
            m_searchPaths.erase(it);
        }
    }

    void SparkAssetManager::ReleaseAsset(const AssetId& id)
    {
        std::lock_guard lock(m_mutex);
        m_assets.erase(id);
    }

    void SparkAssetManager::RegisterAssetLoader(eastl::unique_ptr<AssetLoader> loader, AssetType type)
    {
        std::lock_guard lock(m_mutex);
        m_assetLoaders[type] = eastl::move(loader);
    }

    void SparkAssetManager::RegisterAssetCompiler(eastl::unique_ptr<AssetCompiler> compiler, AssetType type)
    {
        std::lock_guard lock(m_mutex);
        m_assetCompilers[type] = eastl::move(compiler);
    }

    eastl::string SparkAssetManager::ResolvePath(const AssetId& id) const
    {
        auto name = id.GetName().GetStringView();
        for (const auto& searchPath : m_searchPaths)
        {
            std::filesystem::path full = std::filesystem::path(searchPath.c_str()) / name.data();
            if (std::filesystem::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
    }

    void SparkAssetManager::ProcessThread()
    {
        while (true)
        {
            Asset* asset = nullptr;
            {
                std::unique_lock lock(m_mutex);
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
        AssetType type = asset.GetAssetType();

        // Load
        {
            std::lock_guard lock(m_mutex);
            auto loaderIt = m_assetLoaders.find(type);
            if (loaderIt == m_assetLoaders.end())
            {
                LOG_ERROR("No loader registered for asset type {}", static_cast<uint32_t>(type));
                SetAssetStatus(asset, AssetStatus::Error);
                return;
            }
            SetAssetStatus(asset, AssetStatus::Loading);
        }

        // Loader::Load 可能耗时，不持锁执行
        m_assetLoaders[type]->Load(asset);

        if (asset.IsError())
        {
            return;
        }

        // Compile（可选）
        {
            std::lock_guard lock(m_mutex);
            auto compilerIt = m_assetCompilers.find(type);
            if (compilerIt != m_assetCompilers.end())
            {
                SetAssetStatus(asset, AssetStatus::Compiling);
            }
            else
            {
                SetAssetStatus(asset, AssetStatus::Ready);
                return;
            }
        }

        m_assetCompilers[type]->Compile(asset);

        if (!asset.IsError())
        {
            SetAssetStatus(asset, AssetStatus::Ready);
        }
    }
}
