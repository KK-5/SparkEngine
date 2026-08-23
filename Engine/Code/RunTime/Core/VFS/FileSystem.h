#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

namespace Spark
{
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
    };
}
