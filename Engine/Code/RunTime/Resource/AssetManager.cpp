#include "AssetManager.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <VFS/FileSystem.h>
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
        m_fileSystem = Service<FileSystem>::Get();
        ASSERT(m_fileSystem, "[SparkAssetManager] No FileSystem registered. Create VFSSystem "
                             "and mount before Init.");

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
        m_fileSystem = nullptr;
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
        // The one check: every builder below dereferences ctx.fileSystem, so nothing is
        // dispatched without one.
        if (!m_fileSystem)
        {
            LOG_ERROR("[SparkAssetManager] No FileSystem; cannot process {}",
                asset.GetAssetId().GetPath());
            asset.SetStatus(AssetStatus::Error);
            return;
        }

        AssetBuildContext ctx;
        ctx.id         = asset.GetAssetId();
        ctx.type       = asset.GetAssetType();
        ctx.fileSystem = m_fileSystem;
        ctx.db         = m_db.get();

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

    AssetId SparkAssetManager::MakeAssetIdForType(eastl::string_view virtualPath, AssetType type)
    {
        const eastl::string_view path = virtualPath;
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

    AssetId SparkAssetManager::MakeAssetId(eastl::string_view virtualPath)
    {
        AssetType type = GetSupportAssetType(virtualPath);
        if (type == AssetType::Unknown)
        {
            return AssetId();
        }

        // Existence is still checked here, so a bad path fails at the call site that wrote
        // it rather than at load -- the same place the old search-path lookup failed.
        if (!m_fileSystem || m_fileSystem->ToPhysical(virtualPath).empty())
        {
            LOG_ERROR("[SparkAssetManager] Cannot resolve: {}", virtualPath);
            return AssetId();
        }

        return MakeAssetIdForType(virtualPath, type);
    }

    void SparkAssetManager::AssetRegistry()
    {
        if (!m_fileSystem)
        {
            return;
        }

        for (const eastl::string& mount : m_fileSystem->GetMountNames())
        {
            eastl::string root = mount;
            root += "://";

            m_fileSystem->IterateDirectory(root, [this](eastl::string_view virtualPath)
            {
                const AssetType type = GetSupportAssetType(virtualPath);
                if (type == AssetType::Unknown)
                {
                    return;
                }

                AssetId id = MakeAssetIdForType(virtualPath, type);
                if (!id.IsValid() || m_db->Find(id))
                {
                    return;
                }

                Ptr<Asset> asset = CreateAsset(id, type);
                if (asset)
                {
                    m_db->InsertOrGet(id, asset);
                }
            });
        }
    }
}
