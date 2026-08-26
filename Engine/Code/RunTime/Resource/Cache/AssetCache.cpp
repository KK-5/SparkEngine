#include "AssetCache.h"

#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include <Log/ILogSystem.h>
#include <Serialization/Json.h>
#include <VFS/FileSystem.h>

#include <Resource/AssetJsonSerializer.h>

#include "CacheFormat.h"

#ifndef SPARK_CACHE_BUILD_TAG
    #define SPARK_CACHE_BUILD_TAG "Unknown"
#endif

namespace Spark::Resource
{
    namespace
    {
        //! FNV-1a, local until something else needs a 64-bit hash.
        constexpr uint64_t kHashSeed  = 14695981039346656037ull;
        constexpr uint64_t kHashPrime = 1099511628211ull;

        void Fold(uint64_t& hash, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= kHashPrime;
            }
        }

        void Fold(uint64_t& hash, eastl::string_view text)
        {
            Fold(hash, text.data(), text.size());
        }

        void Fold(uint64_t& hash, uint64_t value)
        {
            Fold(hash, &value, sizeof(value));
        }

        bool HasExtension(eastl::string_view path, const char* extension)
        {
            const eastl::string_view ext(extension);
            return path.size() > ext.size()
                && path.compare(path.size() - ext.size(), ext.size(), ext.data()) == 0;
        }

        eastl::string MakeEntryPath(uint64_t key, const char* extension)
        {
            char hex[17];
            snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(key));

            eastl::string path = kCacheMountName;
            path += "://";
            path += hex[0];
            path += hex[1];
            path += '/';
            path += hex;
            path += extension;
            return path;
        }

        //! Split `<stem>.<ext>`. The key is hex and the mount name carries no dot, so the
        //! last dot is always the extension's.
        size_t ExtensionDot(const eastl::string& path)
        {
            const size_t dot = path.rfind('.');
            return dot == eastl::string::npos ? path.size() : dot;
        }

        //! `<stem>.<n><ext>` -- a sibling of the root payload, same format, same key.
        eastl::string SubPayloadPath(const CacheEntry& entry, size_t index)
        {
            const size_t dot = ExtensionDot(entry.path);

            char suffix[24];
            snprintf(suffix, sizeof(suffix), ".%zu", index);

            eastl::string path(entry.path.c_str(), dot);
            path += suffix;
            path.append(entry.path.c_str() + dot, entry.path.size() - dot);
            return path;
        }

        eastl::string ManifestPath(const CacheEntry& entry)
        {
            eastl::string path(entry.path.c_str(), ExtensionDot(entry.path));
            path += ".unit";
            return path;
        }

        constexpr const char* kManifestIdentityKey = "identity";
        constexpr const char* kManifestSubAssetsKey = "subAssets";
    }

    AssetCache::AssetCache(const FileSystem& fileSystem)
        : m_fileSystem(fileSystem)
    {
        for (const eastl::string& mount : m_fileSystem.GetMountNames())
        {
            if (mount == kCacheMountName)
            {
                m_enabled = true;
                break;
            }
        }

        if (!m_enabled)
        {
            LOG_INFO("[AssetCache] No '{}://' mount; every asset will be cooked from source.",
                kCacheMountName);
        }
    }

    CacheEntry AssetCache::EntryFor(const AssetId& id) const
    {
        if (!m_enabled)
        {
            return {};
        }

        if (id.IsSubAsset())
        {
            return {};
        }

        const CacheFormat format = GetCacheFormat(id.GetAssetType());
        if (format.version == 0)
        {
            return {};
        }

        // The source already IS this type's cooked form (an authored .ktx2). An entry would
        // be a copy nothing reads: the source wins the next load either way.
        if (HasExtension(id.GetPath(), format.extension))
        {
            return {};
        }

        // The one file the key can describe -- an asset with more inputs than its own path
        // (a shader's includes, a .gltf's .bin) is where this cache's scope ends.
        const FileStamp source = m_fileSystem.GetFileStamp(id.GetPath());
        if (!source.IsValid())
        {
            return {};
        }

        // The descriptor enters by serialized value, not by AssetDescriptor::Hash(): that
        // is a lossy digest that today does not even cover every field, and unlike the
        // identity hash a cache has no by-value comparison to fall back on.
        JsonValue json;
        if (!AssetIdToJson(id, json))
        {
            return {};
        }
        const std::string canonical = json.dump();

        uint64_t key = kHashSeed;
        Fold(key, eastl::string_view("SparkAssetCache"));
        Fold(key, eastl::string_view(SPARK_CACHE_BUILD_TAG));
        Fold(key, eastl::string_view(canonical.c_str(), canonical.size()));
        Fold(key, source.m_modifiedTime);
        Fold(key, source.m_size);
        Fold(key, static_cast<uint64_t>(format.version));

        CacheEntry entry;
        entry.path     = MakeEntryPath(key, format.extension);
        entry.identity = eastl::string(canonical.c_str(), canonical.size());
        return entry;
    }

    bool AssetCache::ReadUnit(const CacheEntry& entry, CacheUnit& out) const
    {
        if (!entry.IsCacheable())
        {
            return false;
        }

        // Exists first: ReadFile logs a missing file as an error, and a cold cache misses
        // on every asset. The manifest is written last, so its absence is the miss.
        const eastl::string manifestPath = ManifestPath(entry);
        eastl::vector<uint8_t> manifestBytes;
        if (!m_fileSystem.Exists(manifestPath)
            || !m_fileSystem.ReadFile(manifestPath, manifestBytes))
        {
            return false;
        }

        const JsonValue manifest = JsonValue::parse(
            manifestBytes.begin(), manifestBytes.end(), nullptr, /*allow_exceptions=*/ false);
        if (manifest.is_discarded() || !manifest.is_object())
        {
            LOG_WARN("[AssetCache] Unreadable manifest {}", manifestPath.c_str());
            return false;
        }

        // Same collision defense the payloads carry, one level up: it covers the manifest
        // itself, which has no format of its own to hide an identity in.
        const auto identity = manifest.find(kManifestIdentityKey);
        if (identity == manifest.end() || !identity->is_string()
            || identity->get<std::string>() != std::string(entry.identity.c_str()))
        {
            LOG_WARN("[AssetCache] Manifest {} belongs to another asset.", manifestPath.c_str());
            return false;
        }

        CacheUnit unit;
        if (!m_fileSystem.Exists(entry.path) || !m_fileSystem.ReadFile(entry.path, unit.root))
        {
            LOG_WARN("[AssetCache] Unit {} has a manifest but no root payload.",
                manifestPath.c_str());
            return false;
        }

        const auto subs = manifest.find(kManifestSubAssetsKey);
        if (subs != manifest.end() && subs->is_array())
        {
            unit.subs.reserve(subs->size());
            for (const JsonValue& subJson : *subs)
            {
                CacheSubPayload sub;
                sub.id = AssetIdFromJson(subJson);
                if (!sub.id.IsValid())
                {
                    LOG_WARN("[AssetCache] Unit {} names a sub-asset it cannot resolve.",
                        manifestPath.c_str());
                    return false;
                }

                const eastl::string subPath = SubPayloadPath(entry, unit.subs.size());
                if (!m_fileSystem.Exists(subPath) || !m_fileSystem.ReadFile(subPath, sub.bytes))
                {
                    LOG_WARN("[AssetCache] Unit {} is missing payload {}.",
                        manifestPath.c_str(), subPath.c_str());
                    return false;
                }
                unit.subs.push_back(eastl::move(sub));
            }
        }

        out = eastl::move(unit);
        return true;
    }

    bool AssetCache::WriteUnit(const CacheEntry& entry, const CacheUnit& unit) const
    {
        if (!entry.IsCacheable() || unit.root.empty())
        {
            return false;
        }

        JsonValue subIds = JsonValue::array();
        for (const CacheSubPayload& sub : unit.subs)
        {
            // A sub with no bytes means its builder declined to serialize; the unit cannot
            // be stored without it, so the whole write is off.
            JsonValue subJson;
            if (sub.bytes.empty() || !AssetIdToJson(sub.id, subJson))
            {
                return false;
            }
            subIds.push_back(eastl::move(subJson));
        }

        if (!m_fileSystem.WriteFile(entry.path, unit.root.data(), unit.root.size()))
        {
            return false;
        }
        for (size_t i = 0; i < unit.subs.size(); ++i)
        {
            const eastl::string subPath = SubPayloadPath(entry, i);
            const eastl::vector<uint8_t>& bytes = unit.subs[i].bytes;
            if (!m_fileSystem.WriteFile(subPath, bytes.data(), bytes.size()))
            {
                return false;
            }
        }

        JsonValue manifest = JsonValue::object();
        manifest[kManifestIdentityKey]  = std::string(entry.identity.c_str());
        manifest[kManifestSubAssetsKey] = eastl::move(subIds);

        const std::string text = manifest.dump();
        return m_fileSystem.WriteFile(ManifestPath(entry),
            reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
}
