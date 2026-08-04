#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "ImageAsset.h"

namespace Spark::Resource
{
    //! Whether `path` names an image that is stored ALREADY COMPILED (a KTX2 container).
    //! Loading one yields an ImageAssetData, not an ImageAssetRawData, and it must not be
    //! run through ImageAssetCompiler -- mip generation and BCn would re-process a finished
    //! payload. This is the same test the asset cache will use once compiled images are
    //! written back to disk.
    bool IsCompiledImagePath(eastl::string_view path);

    class ImageAssetLoader
    {
    public:
        ImageAssetLoader() = default;
        ~ImageAssetLoader() = default;

        void SetSearchPaths(const eastl::vector<eastl::string>& searchPaths)
        {
            m_searchPaths = searchPaths;
        }

        //! `outIsCompiled` reports which form came back: false = ImageAssetRawData (decoded
        //! source pixels, needs compiling), true = ImageAssetData (finished payload, skip
        //! the compiler). Not defaultable on purpose -- a caller that downcasts the result
        //! has to acknowledge the distinction.
        UniquePtr<AssetData> Load(const AssetId& id, bool& outIsCompiled);

        static UniquePtr<AssetData> DecodeFromMemory(
            const uint8_t* bytes, size_t byteCount, eastl::string_view sourceLabel);

    private:
        eastl::string ResolvePath(const AssetId& id) const;

        //! KTX2 -> ImageAssetData. 2D, single layer, single face, uncompressed containers
        //! only; anything else is rejected rather than guessed at.
        UniquePtr<AssetData> LoadKtx2(const eastl::string& path);

        eastl::vector<eastl::string> m_searchPaths;
    };
}
