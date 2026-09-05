#include "AssetManager.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <VFS/FileSystem.h>
#include "AssetDataBase.h"
#include "AssetBuildContext.h"
#include "Cache/AssetCache.h"
#include "Bus/AssetBuildBus.h"
#include "Bus/AssetBus.h"
#include <filesystem>
#include "Image/ImageAssetBuilder.h"
#include "Image/ImageAsset.h"
#include "Material/MaterialAsset.h"
#include "Material/MaterialAssetBuilder.h"
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

        // Reads the mount table once, so cache:// has to be mounted by now.
        m_cache = MakeUnique<AssetCache>(*m_fileSystem);

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

        m_materialBuilder = CreateSystem<MaterialAssetBuilder>();
        m_materialBuilder->Init();

        m_shutdown = false;
        m_processThread = std::thread(&SparkAssetManager::ProcessThread, this);

        // After the builders: CreateAsset wants one registered for the type.
        FileEventBus::Handler::BusConnect();
    }

    void SparkAssetManager::ShutdownInternal()
    {
        if (FileEventBus::Handler::BusIsConnected())
        {
            FileEventBus::Handler::BusDisconnect();
        }

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
        m_materialBuilder.reset();
        m_modelBuilder.reset();
        m_shaderBuilder.reset();
        m_imageBuilder.reset();

        {
            std::lock_guard lock(m_queueMutex);
            m_pendingQueue.swap(eastl::queue<Asset*>());
        }
        m_cache.reset();
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

    Ptr<Asset> SparkAssetManager::CreateAsset(const AssetId& id)
    {
        Ptr<Asset> result;
        AssetBuildBus::EventResult(result, id.GetAssetType(), &AssetBuildEvents::CreateAsset, id);
        if (!result)
        {
            LOG_ERROR("[SparkAssetManager] No builder registered for AssetType {}",
                static_cast<uint32_t>(id.GetAssetType()));
        }
        return result;
    }

    Ptr<Asset> SparkAssetManager::FindAsset(const AssetId& id) const
    {
        return m_db ? m_db->Find(id) : nullptr;
    }

    Ptr<Asset> SparkAssetManager::LoadAsset(const AssetId& id)
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

        Ptr<Asset> created = CreateAsset(id);
        if (!created)
        {
            return nullptr;
        }

        Ptr<Asset> stored = m_db->InsertOrGet(id, created);
        if (stored.get() != created.get())
        {
            // Lost the race: someone registered first, hand back theirs.
            return stored;
        }

        ProcessAsset(*stored);
        return stored;
    }

    Ptr<Asset> SparkAssetManager::RequestAsset(const AssetId& id)
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

        Ptr<Asset> created = CreateAsset(id);
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

        if (ext == kMaterialExtension)
        {
            return AssetType::Material;
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

    void SparkAssetManager::ReleaseAsset(const AssetId& id, const Asset* self)
    {
        if (m_db)
        {
            m_db->Remove(id, self);
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

        const AssetType type = asset.GetAssetType();

        // A sub-asset has no source of its own: its bytes are inside its parent's file, and
        // only that parent's Compile knows how to get them out. Every builder's Load would
        // instead read the parent's file as if it were this asset -- a .glb decoded as an
        // image -- so the failure is named here rather than left to look like a corrupt
        // file. Its parent's build publishes it; to use one on its own, extract it into an
        // asset of its own first.
        if (asset.GetAssetId().IsSubAsset())
        {
            LOG_ERROR("[SparkAssetManager] '{}:{}' is a sub-asset and cannot be built on its "
                      "own; it is published by the build of '{}'.",
                asset.GetAssetId().GetPath(), asset.GetAssetId().GetSubLabel(),
                asset.GetAssetId().GetPath());
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        AssetBuildContext ctx;
        ctx.id         = asset.GetAssetId();
        ctx.fileSystem = m_fileSystem;

        asset.SetStatus(AssetStatus::Loading);

        // Not cacheable yields an empty entry that every call below declines, so the
        // uncached case needs no second code path.
        const CacheEntry entry = m_cache->EntryFor(ctx.id);
        const eastl::string_view identity(entry.identity.c_str(), entry.identity.size());

        CacheUnit cached;
        if (m_cache->ReadUnit(entry, cached))
        {
            UniquePtr<AssetData> restored;
            AssetBuildBus::EventResult(restored, type, &AssetBuildEvents::Deserialize,
                cached.root.data(), cached.root.size(), identity);

            // One rejected payload rebuilds the whole unit: the entry was written as a
            // whole, so a rebuild is the only thing that can put it back that way.
            eastl::vector<PendingPublish> pending;
            if (restored && RestoreSubAssets(cached, pending))
            {
                for (PendingPublish& sub : pending)
                {
                    Publish(sub);
                }
                asset.SetDataReady(eastl::move(restored));
                AssetBus::Event(type, &AssetBus::Events::OnAssetReady, asset);
                return;
            }

            // Nothing to clean up: the rebuild below writes back to these same paths.
            LOG_WARN("[SparkAssetManager] Rebuilding {}: its cache entry was rejected.",
                asset.GetAssetId().GetPath());
        }

        // No shortcut between the two stages: Load only produces raw, Compile only produces
        // the payload. Nothing can skip Compile and with it what is declared there.
        AssetBuildBus::Event(type, &AssetBuildEvents::Load, ctx);
        if (!ctx.rawData)
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        asset.SetStatus(AssetStatus::Compiling);
        AssetBuildBus::Event(type, &AssetBuildEvents::Compile, ctx);
        if (!ctx.compiledData)
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        // Build every declared sub-asset before publishing any of them: publishing cannot
        // be taken back, so "one sub-asset failed" has to be answerable while nothing is
        // visible yet.
        eastl::vector<PendingPublish> pending;
        if (!BuildSubAssets(ctx, pending))
        {
            asset.SetStatus(AssetStatus::Error);
            AssetBus::Event(type, &AssetBus::Events::OnAssetError, asset);
            return;
        }

        // Whether this is worth storing was decided by EntryFor; a builder that declines
        // says so with an empty blob, and WriteUnit then stores nothing at all -- a unit
        // missing one of its payloads would come back short.
        CacheUnit cooked;
        AssetBuildBus::EventResult(cooked.root, type, &AssetBuildEvents::Serialize,
            *ctx.compiledData, identity);
        cooked.subs.reserve(pending.size());
        for (PendingPublish& sub : pending)
        {
            const AssetId&      subId       = sub.asset->GetAssetId();
            const eastl::string subIdentity = AssetCache::IdentityFor(subId);

            CacheSubPayload payload;
            payload.id = subId;
            AssetBuildBus::EventResult(payload.bytes, subId.GetAssetType(),
                &AssetBuildEvents::Serialize, *sub.data,
                eastl::string_view(subIdentity.c_str(), subIdentity.size()));
            cooked.subs.push_back(eastl::move(payload));
        }
        m_cache->WriteUnit(entry, cooked);

        // Nothing below can fail. Sub-assets first, so anyone woken by the root's Ready
        // finds the whole unit there.
        for (PendingPublish& sub : pending)
        {
            Publish(sub);
        }

        // Ordinary assets in their own files, loaded like any other. Before the root goes
        // Ready for the same reason its sub-assets are.
        for (const AssetId& dependency : ctx.dependencies)
        {
            LoadAsset(dependency);
        }

        asset.SetDataReady(eastl::move(ctx.compiledData));
        AssetBus::Event(type, &AssetBus::Events::OnAssetReady, asset);
    }

    bool SparkAssetManager::RestoreSubAssets(CacheUnit& unit,
                                             eastl::vector<PendingPublish>& out)
    {
        out.reserve(unit.subs.size());

        for (CacheSubPayload& sub : unit.subs)
        {
            Ptr<Asset> created = CreateAsset(sub.id);
            if (!created)
            {
                return false;
            }

            // Recomputed, not stored: a sub-asset's identity is a pure function of the id
            // the manifest listed.
            const eastl::string identity = AssetCache::IdentityFor(sub.id);

            UniquePtr<AssetData> restored;
            AssetBuildBus::EventResult(restored, sub.id.GetAssetType(),
                &AssetBuildEvents::Deserialize, sub.bytes.data(), sub.bytes.size(),
                eastl::string_view(identity.c_str(), identity.size()));
            if (!restored)
            {
                return false;
            }

            out.push_back({eastl::move(created), eastl::move(restored)});
        }

        return true;
    }

    bool SparkAssetManager::BuildSubAssets(AssetBuildContext& ctx,
                                           eastl::vector<PendingPublish>& out)
    {
        out.reserve(ctx.subAssets.size());

        for (SubAssetEntry& sub : ctx.subAssets)
        {
            const AssetType subType = sub.id.GetAssetType();

            // Created but not registered: a failure further down must leave nothing behind,
            // and this is the only step of publishing that can fail.
            Ptr<Asset> created = CreateAsset(sub.id);
            if (!created)
            {
                return false;
            }

            AssetBuildContext child = ctx.MakeChild(sub.id);
            child.rawData    = eastl::move(sub.rawData);
            child.sourceData = sub.sourceData;
            child.sourceSize = sub.sourceSize;

            // Already-raw sub-assets skip Load; the rest read the bytes their parent points
            // at. Either way Compile runs -- a sub-asset is not a payload arriving finished.
            if (!child.rawData)
            {
                AssetBuildBus::Event(subType, &AssetBuildEvents::Load, child);
                if (!child.rawData)
                {
                    LOG_ERROR("[SparkAssetManager] Sub-asset Load failed: {}:{}",
                        sub.id.GetPath(), sub.id.GetSubLabel());
                    return false;
                }
            }

            AssetBuildBus::Event(subType, &AssetBuildEvents::Compile, child);
            if (!child.compiledData)
            {
                LOG_ERROR("[SparkAssetManager] Sub-asset Compile failed: {}:{}",
                    sub.id.GetPath(), sub.id.GetSubLabel());
                return false;
            }

            // A unit's dependencies are the union of its members'.
            ctx.dependencies.insert(ctx.dependencies.end(),
                child.dependencies.begin(), child.dependencies.end());

            ASSERT(child.subAssets.empty(),
                "[SparkAssetManager] '{}:{}' declared sub-assets of its own; a build unit is "
                "flat and this would be silently discarded",
                sub.id.GetPath().c_str(), sub.id.GetSubLabel().c_str());

            out.push_back({eastl::move(created), eastl::move(child.compiledData)});
        }

        return true;
    }

    void SparkAssetManager::Publish(PendingPublish& entry)
    {
        // On a re-process this returns the instance everyone already holds, so that is the
        // one to hand the fresh data to.
        const AssetId& id = entry.asset->GetAssetId();
        Ptr<Asset> stored = m_db->InsertOrGet(id, entry.asset);
        stored->SetDataReady(eastl::move(entry.data));
        AssetBus::Event(id.GetAssetType(), &AssetBus::Events::OnAssetReady, *stored);
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
                return AssetId::Of(path, {}, AssetType::Image,
                    isHdr ? ImageAsset::DefaultHDRDescriptor() : ImageAsset::DefaultDescriptor());
            }
            case AssetType::Shader:   return AssetId::Of<ShaderAsset>(path);
            case AssetType::Model:    return AssetId::Of<ModelAsset>(path);
            case AssetType::Material: return AssetId::Of<MaterialAsset>(path);
            default:                  return AssetId();
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

    bool SparkAssetManager::RegisterFile(eastl::string_view virtualPath)
    {
        const AssetType type = GetSupportAssetType(virtualPath);
        if (type == AssetType::Unknown)
        {
            return false;
        }

        AssetId id = MakeAssetIdForType(virtualPath, type);
        if (!id.IsValid() || m_db->Find(id))
        {
            return false;
        }

        Ptr<Asset> asset = CreateAsset(id);
        if (!asset)
        {
            return false;
        }

        m_db->InsertOrGet(id, asset);
        return true;
    }

    void SparkAssetManager::AssetRegistry()
    {
        if (!m_fileSystem)
        {
            return;
        }

        // A stack rather than recursion: nothing bounds an asset tree's depth.
        eastl::vector<eastl::string> pending;

        for (const eastl::string& mount : m_fileSystem->GetMountNames())
        {
            // Walking it would register every `.ktx2` entry as an image asset of its own,
            // into a database that never evicts.
            if (mount == kCacheMountName)
            {
                continue;
            }

            eastl::string root = mount;
            root += "://";
            pending.push_back(eastl::move(root));

            while (!pending.empty())
            {
                const eastl::string dir = eastl::move(pending.back());
                pending.pop_back();

                m_fileSystem->ListDirectory(dir,
                    [this, &pending](eastl::string_view virtualPath, bool isDirectory)
                {
                    if (isDirectory)
                    {
                        pending.push_back(eastl::string(virtualPath.data(), virtualPath.size()));
                    }
                    else
                    {
                        RegisterFile(virtualPath);
                    }
                });
            }
        }
    }

    AssetId SparkAssetManager::WriteAssetFile(eastl::string_view virtualPath,
                                              const uint8_t* data, size_t size)
    {
        if (!m_fileSystem)
        {
            return AssetId();
        }

        // Before the write: a file we cannot register is one nothing can ever load.
        const AssetType type = GetSupportAssetType(virtualPath);
        if (type == AssetType::Unknown)
        {
            LOG_ERROR("[SparkAssetManager] '{}' has no extension we build; not written.",
                      virtualPath);
            return AssetId();
        }

        if (!m_fileSystem->WriteFile(virtualPath, data, size))
        {
            LOG_ERROR("[SparkAssetManager] Could not write '{}'.", virtualPath);
            return AssetId();
        }

        // False means "already registered", which is what overwriting looks like -- the id
        // is what says this worked.
        RegisterFile(virtualPath);

        const AssetId id = MakeAssetIdForType(virtualPath, type);

        // Every save passes through here, so the notification belongs here and not in
        // SaveAsset. Success only: a failed save leaves nothing to undo.
        AssetBus::Event(type, &AssetBusTraits::OnAssetSaved, id);

        return id;
    }

    AssetId SparkAssetManager::SaveAsset(const Asset& asset, eastl::string_view virtualPath)
    {
        const AssetType type = GetSupportAssetType(virtualPath);
        if (type == AssetType::Unknown)
        {
            LOG_ERROR("[SparkAssetManager] '{}' has no extension we build; not saved.",
                      virtualPath);
            return AssetId();
        }

        AssetData* data = asset.GetData<AssetData>();
        if (!data)
        {
            LOG_ERROR("[SparkAssetManager] Nothing to save to '{}': the asset holds no data.",
                      virtualPath);
            return AssetId();
        }

        // No handler leaves it false, which is the answer we want.
        bool prepared = false;
        AssetBuildBus::EventResult(prepared, type, &AssetBuildEvents::PrepareToSave,
                                   *data, virtualPath);
        if (!prepared)
        {
            return AssetId();
        }

        eastl::vector<uint8_t> bytes;
        AssetBuildBus::EventResult(bytes, type, &AssetBuildEvents::Serialize, *data,
                                   eastl::string_view());
        if (bytes.empty())
        {
            LOG_ERROR("[SparkAssetManager] AssetType {} produced no bytes for '{}'.",
                      static_cast<uint32_t>(type), virtualPath);
            return AssetId();
        }

        return WriteAssetFile(virtualPath, bytes.data(), bytes.size());
    }

    void SparkAssetManager::OnFileAdded(eastl::string virtualPath)
    {
        RegisterFile(virtualPath);
    }

    void SparkAssetManager::OnFileWatchOverflow()
    {
        AssetRegistry();
    }
}
