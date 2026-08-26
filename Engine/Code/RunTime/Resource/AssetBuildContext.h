#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "Asset.h"
#include "AssetTypes.h"


namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    //! One sub-asset a Compile declared. Where its data comes from is expressed by which
    //! slot is filled -- `rawData` for something the parent already built (a bake's faces),
    //! `sourceData` for bytes sitting inside the parent's file. Both are raw: a sub-asset
    //! gets the same Compile as everything else.
    //!
    //! Deliberately not an AssetBuildContext: a declaration has no sub-declarations of its
    //! own, and one level deep is worth having as a type guarantee rather than an assert.
    struct SubAssetEntry
    {
        AssetId              id;
        UniquePtr<AssetData> rawData;
        const uint8_t*       sourceData = nullptr;
        size_t               sourceSize = 0;
    };

    class AssetBuildContext
    {
    public:
        AssetId   id;

        UniquePtr<AssetData> rawData;        ///< Load's output, Compile's input
        UniquePtr<AssetData> compiledData;   ///< Compile's output

        //! Compile's second output. A builder declares; ProcessAsset publishes. Only the
        //! layer holding the root's status and its cache entry can draw the transaction
        //! boundary "these sub-assets and this root, all or none".
        eastl::vector<SubAssetEntry> subAssets;

        //! Ordinary assets in files of their own (a glTF's external texture URIs). Each has
        //! its own stamp and its own cache key, so it is a dependency, not a sub-asset.
        eastl::vector<AssetId> dependencies;

        //! Non-null when Load decodes from memory instead of reading a file.
        const uint8_t* sourceData = nullptr;
        size_t         sourceSize = 0;

        const FileSystem*            fileSystem{nullptr};

        AssetBuildContext() = default;
        AssetBuildContext(const AssetBuildContext&) = delete;
        AssetBuildContext& operator=(const AssetBuildContext&) = delete;
        AssetBuildContext(AssetBuildContext&&) = default;
        AssetBuildContext& operator=(AssetBuildContext&&) = default;

        eastl::string ResolvePath(eastl::string_view virtualPath) const;

        //! A context for one declared sub-asset. Carries the file system across and nothing
        //! else: the sub's data comes from its SubAssetEntry, not from its parent's slots.
        AssetBuildContext MakeChild(AssetId subId) const;
    };

    //! Resolve `relative` against the directory of `virtualPath`, lexically. Used for a
    //! glTF's external texture URIs, which are relative to the model file.
    eastl::string ResolveSiblingVirtualPath(eastl::string_view virtualPath,
                                            eastl::string_view relative);
}
