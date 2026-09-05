#pragma once

#include <cstdint>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

namespace Spark
{
    //! Enough of a file's state to tell "unchanged" from "rebuilt" without reading it.
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

        //! One level, directories included. The virtual paths are valid only for that call.
        //!
        //! The primitive: a recursive walk is this plus a stack at the call site, while a
        //! recursive files-only visit cannot express one level, nor name an empty directory.
        virtual void ListDirectory(
            eastl::string_view virtualDir,
            eastl::function<void(eastl::string_view virtualPath, bool isDirectory)> visit) const = 0;

        //! Whole file into `out`. False on any failure including a short read: a partial
        //! buffer is indistinguishable from a truncated payload downstream.
        virtual bool ReadFile(eastl::string_view virtualPath,
                              eastl::vector<uint8_t>& out) const = 0;

        //! Atomic: a uniquely-suffixed temporary alongside the target, then a rename.
        //! Creates parent directories. Two writers of the same target are safe; each has its
        //! own temporary, and the second rename replaces an identical file.
        //!
        //! The point is that the target path never names a half-written file -- a plain
        //! write that dies partway leaves one that still passes an existence check.
        virtual bool WriteFile(eastl::string_view virtualPath,
                               const uint8_t* data, size_t size) const = 0;

        //! Regular files only. A directory answers false: callers here mean "can I read
        //! this", not "is there something at this path".
        virtual bool Exists(eastl::string_view virtualPath) const = 0;

        //! Invalid when the path names no readable file.
        virtual FileStamp GetFileStamp(eastl::string_view virtualPath) const = 0;
    };
}
