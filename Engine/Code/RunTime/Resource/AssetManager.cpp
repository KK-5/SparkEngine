#include "AssetManager.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include "AssetDataBase.h"
#include "AssetBuildContext.h"
#include "Bus/AssetBuildBus.h"
#include "Bus/AssetBus.h"
#include <filesystem>
#include "Image/ImageAssetBuilder.h"
#include "Image/ImageAsset.h"
#include "Shader/ShaderAssetBuilder.h"
#include "Shader/ShaderAsset.h"
#include "Model/ModelAssetBuilder.h"
#include "Model/ModelAsset.h"

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
            if (existing->IsReady())
            {
                return existing;
            }
            if (!existing->IsLoading())
            {
                ProcessAsset(*existing);
            }
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
            if (existing->IsReady())
            {
                return existing;
            }
            if (!existing->IsLoading())
            {
                EnqueueForProcessing(*existing);
            }
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

    eastl::vector<eastl::string> SparkAssetManager::GetSearchPathes() const
    {
        return m_searchPaths;
    }

    AssetType SparkAssetManager::GetSupportAssetType(eastl::string_view file)
    {
        eastl::string_view ext;
        const auto pos = file.rfind('.');
        if (pos != eastl::string_view::npos)
        {
            ext = file.substr(pos);
        }

        if (ext.empty())
        {
            return AssetType::Unknown;
        }

        if (ext == ".hlsl")
        {
            return AssetType::Shader;
        }

        if (ext == ".gltf" || ext == ".glb")
        {
            return AssetType::Model;
        }

        // .ktx2 is the already-compiled form (see IsCompiledImagePath): same asset type,
        // but it bypasses the compiler rather than feeding it.
        if (ext == ".png"  || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp"  || ext == ".tga" || ext == ".hdr" ||
            ext == ".psd"  || ext == ".gif" || ext == ".pic" ||
            ext == ".pnm"  || ext == ".svg" || ext == ".ktx2")
        {
            return AssetType::Image;
        }

        return AssetType::Unknown;
    }

    void SparkAssetManager::ReleaseAsset(const AssetId& id)
    {
        if (m_db)
        {
            m_db->Remove(id);
        }
    }

    bool SparkAssetManager::InitEnvironmentBaker()
    {
        if (!m_imageBuilder)
        {
            LOG_ERROR("[SparkAssetManager] InitEnvironmentBaker before Init.");
            return false;
        }
        return m_imageBuilder->InitEnvironmentBaker();
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
        if (!ctx.rawData && !ctx.compiledData)
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(ctx.type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        // Load can hand back a finished payload instead of a raw one -- an authored
        // already-compiled file (a .ktx2 image), and later a cache hit. Compiling it again
        // would re-process a finished product, so the whole stage is skipped.
        //
        // Note this also skips Compile's SIDE EFFECTS: CompileEnvironmentCubemap publishes
        // the two IBL sub-assets from there, so caching a cubemap will have to cache its
        // children too, not just its own payload.
        if (!ctx.compiledData)
        {
            asset.SetStatus(AssetStatus::Compiling);
            AssetBuildBus::Event(ctx.type, &AssetBuildEvents::Compile, ctx);
        }

        if (!ctx.compiledData)
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(ctx.type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        asset.SetDataReady(eastl::move(ctx.compiledData));
        AssetBus::Event(ctx.type, &AssetBus::Events::OnAssetReady, asset);
    }

    AssetId SparkAssetManager::MakeAssetIdForType(eastl::string_view path, AssetType type)
    {
        switch (type)
        {
            case AssetType::Image:
            {
                // HDR files are equirectangular environment maps in this engine, so give
                // them the cubemap descriptor at registration: the descriptor folds into
                // the AssetId hash, making "Foo.hdr" identify the baked cube everywhere.
                // Everyone downstream just references that identity. Per-asset usage
                // overrides belong to a future editor "change asset usage" / ReLoad path.
                eastl::string ext(
                    std::filesystem::path(path.begin(), path.end()).extension().string().c_str());
                for (auto& c : ext)
                {
                    c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                }
                const bool isHdr = (ext == ".hdr");
                return AssetId::Of(path,
                    isHdr ? ImageAsset::DefaultHDRDescriptor() : ImageAsset::DefaultDescriptor());
            }
            case AssetType::Shader: return AssetId::Of<ShaderAsset>(path);
            case AssetType::Model:  return AssetId::Of<ModelAsset>(path);
            default:                return AssetId();
        }
    }

    AssetId SparkAssetManager::MakeAssetId(eastl::string_view path)
    {
        AssetType type = GetSupportAssetType(path);
        if (type == AssetType::Unknown)
        {
            return AssetId();
        }

        eastl::string resolved = ResolveAssetPath(path, SnapshotSearchPaths());
        if (resolved.empty())
        {
            LOG_ERROR("[SparkAssetManager] File not found in any search path: {}", path);
            return AssetId();
        }

        return MakeAssetIdForType(resolved, type);
    }

    void SparkAssetManager::AssetRegistry()
    {
        namespace fs = std::filesystem;

        for (const auto& searchPath : m_searchPaths)
        {
            std::error_code ec;
            if (!fs::exists(searchPath.c_str(), ec) || !fs::is_directory(searchPath.c_str(), ec))
            {
                continue;
            }

            for (auto it = fs::recursive_directory_iterator(searchPath.c_str(), ec),
                      end = fs::recursive_directory_iterator();
                 it != end; it.increment(ec))
            {
                if (ec) { break; }
                if (it->is_directory(ec)) { continue; }

                eastl::string filePath(it->path().generic_string().c_str());
                AssetId id = MakeAssetId(filePath);
                if (!id.IsValid()) { continue; }
                if (m_db->Find(id)) { continue; }

                AssetType type = GetSupportAssetType(filePath);
                Ptr<Asset> asset = CreateAsset(id, type);
                if (asset)
                {
                    m_db->InsertOrGet(id, asset);
                }
            }
        }
    }
}
