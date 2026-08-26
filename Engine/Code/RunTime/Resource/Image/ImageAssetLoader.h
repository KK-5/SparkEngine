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

    //! Bytes in, an ImageRawData out. Every entry point produces raw; what Compile then does
    //! with it is decided by the raw's kind.
    class ImageAssetLoader
    {
    public:
        ImageAssetLoader() = default;
        ~ImageAssetLoader() = default;

        //! An encoded source image -> ImageAssetRawData (decoded pixels).
        UniquePtr<AssetData> LoadSource(const AssetId& id, const FileSystem& fileSystem);

        //! An authored .ktx2 -> ImageEncodedRawData (its bytes, unparsed).
        UniquePtr<AssetData> LoadEncoded(const AssetId& id, const FileSystem& fileSystem);

        //! KTX2 bytes -> ImageAssetData, repacked slice-major / mip-inner. Single-layer 2D
        //! or cube, unsupercompressed; anything else is rejected rather than guessed at.
        //! Serves both a .ktx2's Compile and the cache's read side, so the two cannot
        //! disagree about a layout.
        //!
        //! A non-empty `expectedIdentity` must equal the one stored in the container's
        //! key/value data, which is what catches a cache key collision. An authored file
        //! carries none and passes an empty view. `label` only reaches log messages.
        static UniquePtr<AssetData> LoadKtx2(const uint8_t* bytes, size_t size,
                                             eastl::string_view label,
                                             eastl::string_view expectedIdentity);

        static UniquePtr<AssetData> DecodeFromMemory(
            const uint8_t* bytes, size_t byteCount, eastl::string_view sourceLabel);
    };
}
