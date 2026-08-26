#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Resource/AssetTypes.h>

namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    //! Mounting this is what turns caching on. It must stay invisible to AssetRegistry:
    //! `.ktx2` is a registrable image extension, so a walk over it would register every
    //! cache entry as an asset of its own.
    constexpr const char* kCacheMountName = "cache";

    //! Where an asset's cooked payload belongs and what proves the file found there is
    //! really that asset's -- not the payload itself. Both fields come from one serialized
    //! identity: `path` is its hash, `identity` its text. `path` is computed, never found,
    //! so a cacheable entry may still have nothing at its path.
    struct CacheEntry
    {
        eastl::string path;    ///< cache://3f/3fa9c2b81d4e6075.ktx2 -- one shard level

        //! The identity JSON the key was hashed from, verbatim: written into the payload and
        //! compared on the way back out, so a key collision is caught instead of silently
        //! handing one asset another's pixels. Stored by value because an exact comparison
        //! cannot false-accept, where a second digest would only make a collision rarer.
        eastl::string identity;

        bool IsCacheable() const { return !path.empty(); }
    };

    //! Cache policy in one place -- what the key is made of, where an entry lands. Builders
    //! own only their own format and never see a path or a key.
    class AssetCache
    {
    public:
        //! Reads the mount table once, so mount `cache://` before AssetManager::Init.
        explicit AssetCache(const FileSystem& fileSystem);

        //! Pure computation over the id, its source's stamp and the format version. All
        //! three feed the key, so a touched source addresses a different entry and staleness
        //! is never something anyone has to detect.
        //!
        //! Not cacheable in five cases: no `cache://` mount, a type with no cache format, a
        //! source that already is that format, a sub-asset (its bytes live in a parent file,
        //! so it has nothing to stamp), or an unstampable source.
        CacheEntry EntryFor(const AssetId& id) const;

        //! False on a miss, which is the ordinary first-run outcome and not an error.
        bool Read(const CacheEntry& entry, eastl::vector<uint8_t>& out) const;

        bool Write(const CacheEntry& entry, const eastl::vector<uint8_t>& blob) const;

    private:
        const FileSystem& m_fileSystem;
        bool              m_enabled{false};
    };
}
