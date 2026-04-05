#include "AssetManager.h"

#include <EASTL/algorithm.h>
#include <Log/SpdLogSystem.h>

#include <filesystem>

#include "EBus/AssetBus.h"
#include "Shader/ShaderAsset.h"

#include "Common/CommonAssetLoader.h"
#include "Shader/ShaderAssetCompiler.h"

namespace Spark::Resource
{
    void SparkAssetManager::InitInternal()
    {
        m_shutdown = false;
        RegisterDefaultLoaderAndCompiler();
        m_processThread = std::thread(&SparkAssetManager::ProcessThread, this);
    }

    void SparkAssetManager::ShutdownInternal()
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
    }

    eastl::vector<HashString> SparkAssetManager::Request() const
    {
        return {"LogSystem"_hs};
    }

    HashString SparkAssetManager::GetName() const
    {
        return "AssetManager"_hs;
    }

    Ptr<Asset> SparkAssetManager::CreateAssetByType(const AssetId& id, AssetType type)
    {
        switch (type)
        {
            case AssetType::Shader:
            {
                return Ptr<Asset>(new ShaderAsset(id));
            }
            default:
            {
                return Ptr<Asset>(new Asset(id, type));
            }
        }
    }

    void SparkAssetManager::RegisterDefaultLoaderAndCompiler()
    {
        // Shader
        auto binaryLoader =  eastl::make_unique<BinaryAssetLoader>();
        RegisterAssetLoader(eastl::move(binaryLoader), AssetType::Shader);

        auto compiler = eastl::make_unique<ShaderAssetCompiler>(ShaderBackend::DXIL);
        compiler->AddStageEntry({RHI::ShaderStage::Vertex, "VSMain", "vs_6_0"});
        compiler->AddStageEntry({RHI::ShaderStage::Fragment, "PSMain", "ps_6_0"});
        RegisterAssetCompiler(eastl::move(compiler), AssetType::Shader);

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
        {
            std::lock_guard lock(m_mutex);
            auto it = m_assets.find(id);
            if (it != m_assets.end())
            {
                return Ptr<Asset>(it->second);
            }
        }

        Ptr<Asset> asset = CreateAssetByType(id, type);

        {
            std::lock_guard lock(m_mutex);
            m_assets.insert_or_assign(id, asset.get());
        }

        ProcessAsset(*asset);

        return asset;
    }

    Ptr<Asset> SparkAssetManager::RequestAsset(const AssetId& id, AssetType type)
    {
        std::lock_guard lock(m_mutex);
        auto it = m_assets.find(id);
        if (it != m_assets.end())
        {
            return Ptr<Asset>(it->second);
        }

        Ptr<Asset> asset = CreateAssetByType(id, type);;
        SetAssetStatus(*asset, AssetStatus::Queued);
        m_assets.insert_or_assign(id, asset.get());
        m_pendingQueue.push(asset.get());

        m_cv.notify_one();
        return asset;
    }

    void SparkAssetManager::AddSearchPath(eastl::string_view path)
    {
        std::lock_guard lock(m_mutex);
        m_searchPaths.emplace_back(path.data(), path.size());
        AssetCatalogBus::Broadcast(&AssetCatalogBus::Events::OnAssetSearchPathsChange, m_searchPaths);
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
        AssetCatalogBus::Broadcast(&AssetCatalogBus::Events::OnAssetSearchPathsChange, m_searchPaths);
    }

    void SparkAssetManager::ReleaseAsset(const AssetId& id)
    {
        std::lock_guard lock(m_mutex);
        m_assets.erase(id);
    }

    void SparkAssetManager::RegisterAssetLoader(eastl::unique_ptr<AssetLoader> loader, AssetType type)
    {
        std::lock_guard lock(m_mutex);
        loader->SetSearchPaths(m_searchPaths);
        m_assetLoaders[type] = eastl::move(loader);
    }

    void SparkAssetManager::RegisterAssetCompiler(eastl::unique_ptr<AssetCompiler> compiler, AssetType type)
    {
        std::lock_guard lock(m_mutex);
        m_assetCompilers[type] = eastl::move(compiler);
    }

    /*
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
    */

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
        const AssetId& id = asset.GetAssetId();

        AssetLoader* loader = nullptr;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_assetLoaders.find(type);
            if (it == m_assetLoaders.end())
            {
                LOG_ERROR("No loader registered for asset type {}", static_cast<uint32_t>(type));
                SetAssetStatus(asset, AssetStatus::Error);
                return;
            }
            loader = it->second.get();
            SetAssetStatus(asset, AssetStatus::Loading);
        }

        auto rawData = loader->Load(id);
        if (!rawData)
        {
            SetAssetStatus(asset, AssetStatus::Error);
            return;
        }

        // 查找 Compiler（可选）
        AssetCompiler* compiler = nullptr;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_assetCompilers.find(type);
            if (it != m_assetCompilers.end())
            {
                compiler = it->second.get();
                SetAssetStatus(asset, AssetStatus::Compiling);
            }
        }

        if (compiler)
        {
            auto compiledData = compiler->Compile(id, *rawData);
            if (!compiledData)
            {
                SetAssetStatus(asset, AssetStatus::Error);
                return;
            }
            SetAssetData(asset, eastl::move(compiledData));
        }
        else
        {
            SetAssetData(asset, eastl::move(rawData));
        }

        SetAssetStatus(asset, AssetStatus::Ready);
    }
}
