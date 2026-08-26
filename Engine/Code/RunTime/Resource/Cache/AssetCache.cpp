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

    bool AssetCache::Read(const CacheEntry& entry, eastl::vector<uint8_t>& out) const
    {
        // Exists first: ReadFile logs a missing file as an error, and a cold cache misses
        // on every asset.
        if (!entry.IsCacheable() || !m_fileSystem.Exists(entry.path))
        {
            return false;
        }
        return m_fileSystem.ReadFile(entry.path, out);
    }

    bool AssetCache::Write(const CacheEntry& entry, const eastl::vector<uint8_t>& blob) const
    {
        if (!entry.IsCacheable() || blob.empty())
        {
            return false;
        }
        return m_fileSystem.WriteFile(entry.path, blob.data(), blob.size());
    }
}
