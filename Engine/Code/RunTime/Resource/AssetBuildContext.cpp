#include "AssetBuildContext.h"

#include <VFS/FileSystem.h>

namespace Spark::Resource
{
    eastl::string ResolveSiblingVirtualPath(eastl::string_view virtualPath,
                                            eastl::string_view relative)
    {
        if (relative.empty())
        {
            return {};
        }

        // Everything up to the last separator, keeping the mount prefix. find_last_of never
        // reaches into "mount://" itself, since a mount name carries no '/'.
        const size_t slash = virtualPath.find_last_of('/');
        if (slash == eastl::string_view::npos)
        {
            return {};
        }

        eastl::string out(virtualPath.data(), slash + 1);
        out.append(relative.data(), relative.size());
        return out;
    }

    eastl::string AssetBuildContext::ResolvePath(eastl::string_view virtualPath) const
    {
        return fileSystem ? fileSystem->ToPhysical(virtualPath) : eastl::string();
    }

    AssetBuildContext AssetBuildContext::MakeChild(AssetId subId, AssetType subType) const
    {
        AssetBuildContext child;
        child.id         = eastl::move(subId);
        child.type       = subType;
        child.parentId   = id;
        child.fileSystem = fileSystem;
        child.db         = db;
        return child;
    }
}
