#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "ImageAsset.h"

namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    //! Whether `path` names an image whose authored form is ALREADY COMPILED (a KTX2
    //! container). Nothing to do with the cache: such a file must skip ImageAssetCompiler
    //! because mip generation and BCn would re-process a finished payload.
    bool IsCompiledImagePath(eastl::string_view path);

    //! The KTX2 key/value entry a cache entry stores its asset identity under. Shared by the
    //! compiler's write side and the loader's read side. Outside KTX2's reserved `KTX`
    //! namespace, and frozen -- renaming it strands every existing entry.
    constexpr const char* kImageIdentityKey = "SparkAssetIdentity";

    //! Three entry points, separated by where the bytes come from. Which one the caller
    //! picks is what says whether the result is raw or finished -- there is no out-param
    //! reporting it back.
    class ImageAssetLoader
    {
    public:
        ImageAssetLoader() = default;
        ~ImageAssetLoader() = default;

        //! An encoded source image -> ImageAssetRawData, still to be compiled.
        UniquePtr<AssetData> LoadSource(const AssetId& id, const FileSystem& fileSystem);

        //! An authored .ktx2 -> ImageAssetData, already finished.
        UniquePtr<AssetData> LoadCompiled(const AssetId& id, const FileSystem& fileSystem);

        //! KTX2 bytes -> ImageAssetData. 2D, single layer, single face, unsupercompressed
        //! containers only; anything else is rejected rather than guessed at.
        //!
        //! A non-empty `expectedIdentity` must equal the one stored in the container's
        //! key/value data -- that is the cache read side. An authored file carries none and
        //! passes an empty view. `label` only ever reaches log messages.
        UniquePtr<AssetData> LoadKtx2(const uint8_t* bytes, size_t size,
                                      eastl::string_view label,
                                      eastl::string_view expectedIdentity);

        static UniquePtr<AssetData> DecodeFromMemory(
            const uint8_t* bytes, size_t byteCount, eastl::string_view sourceLabel);
    };
}
