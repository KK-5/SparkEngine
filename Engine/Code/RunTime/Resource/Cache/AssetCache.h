#pragma once

namespace Spark::Resource
{
    //! The mount the cook cache reads and writes through, so cached payloads go over the
    //! same FileSystem as everything else instead of opening a second physical channel.
    //!
    //! Mounting it is what turns caching on: with no `cache://` mount every asset builds
    //! exactly as it did before. Tests give each fixture its own temporary directory here,
    //! since a shared one makes them order-dependent and lets a stale entry mask a real
    //! compiler bug.
    //!
    //! It must stay invisible to AssetRegistry: `.ktx2` is a registrable image extension,
    //! so a walk over this mount would register every cache entry as an asset of its own.
    constexpr const char* kCacheMountName = "cache";
}
