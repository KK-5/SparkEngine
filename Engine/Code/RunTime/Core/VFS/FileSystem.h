#pragma once

#include <cstdint>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

namespace Spark
{
    //! Enough of a file's state to tell "unchanged" from "rebuilt" without reading it.
    //! Feeds the asset cache key, so a touched source yields a different key and the stale
    //! entry simply becomes unreachable -- expiry never has to be decided.
    struct FileStamp
    {
        uint64_t m_modifiedTime{0};   ///< seconds since the filesystem clock's epoch
        uint64_t m_size{0};

        bool IsValid() const { return m_modifiedTime != 0; }
    };

    //! The single place an asset path is resolved. Virtual paths look like
    //! `mount://relative` (e.g. `engine://Shaders/GBuffer.hlsl`); the core invariant is that
    //! AssetId::m_path always holds one, so a physical path only ever appears at the moment
    //! a file is actually read, and only through here.
    //!
    //! Implemented by MountTable; owned and registered by VFSSystem in production.
    class FileSystem
    {
    public:
        virtual ~FileSystem() = default;

        //! Rejects a name that is empty or contains '/' or ':', a name already in use, and a
        //! directory that nests with an existing mount -- overlapping mounts would give one
        //! file two virtual paths, hence two AssetIds.
        virtual void Mount(eastl::string_view name, eastl::string_view physicalDir) = 0;
        virtual void Unmount(eastl::string_view name) = 0;

        //! Empty when the path lies outside every mount: never falls back to the raw path.
        virtual eastl::string ToVirtual(eastl::string_view physicalPath) const = 0;

        //! A table lookup, not a search. The relative segment is normalized ("." and ".."
        //! resolved lexically); empty when ".." escapes the mount root.
        virtual eastl::string ToPhysical(eastl::string_view virtualPath) const = 0;

        virtual eastl::vector<eastl::string> GetMountNames() const = 0;

        //! In registration order, for consumers that must search roots in sequence rather
        //! than look one up -- DXC #include resolution being the only one today.
        virtual eastl::vector<eastl::string> GetPhysicalDirs() const = 0;

        //! Recursive. The callback receives virtual paths valid only for that call.
        virtual void IterateDirectory(eastl::string_view virtualDir,
                                      eastl::function<void(eastl::string_view)> visit) const = 0;

        //! Whole file into `out`, which is resized to the file's size. False on any failure,
        //! including a short read -- a partial buffer would look like a truncated payload to
        //! every caller downstream.
        virtual bool ReadFile(eastl::string_view virtualPath,
                              eastl::vector<uint8_t>& out) const = 0;

        //! Atomic: writes a uniquely-suffixed temporary alongside the target and renames it
        //! into place, creating parent directories as needed. Nothing else may write the
        //! target concurrently, but two writers of the SAME target are safe -- each has its
        //! own temporary, and whichever renames second simply replaces an identical file.
        //!
        //! Crash-safety is the point: a plain write that dies halfway leaves a truncated
        //! file that still passes an existence check.
        virtual bool WriteFile(eastl::string_view virtualPath,
                               const uint8_t* data, size_t size) const = 0;

        //! True only for an existing regular file. A directory answers false: every caller
        //! here is asking "can I read this", not "is there something at this path".
        virtual bool Exists(eastl::string_view virtualPath) const = 0;

        //! An invalid stamp when the path names no readable file. Callers treat that as
        //! "not cacheable" rather than as an error -- an unstampable source is exactly the
        //! case a content-independent cache key cannot describe.
        virtual FileStamp GetFileStamp(eastl::string_view virtualPath) const = 0;
    };
}
