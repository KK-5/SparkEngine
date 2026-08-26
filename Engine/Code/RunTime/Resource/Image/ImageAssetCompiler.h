#pragma once

#include <Base.h>
#include <Resource/Asset.h>
#include <Resource/AssetBuildContext.h>

#include "ImageAsset.h"
#include "EnvironmentBaker.h"

namespace Spark::Resource
{
    class ImageAssetCompiler final
    {
    public:
        ImageAssetCompiler() = default;
        ~ImageAssetCompiler() = default;

        //! The one way an image raw becomes a finished payload: pixels get a mip chain and
        //! BCn, a container gets parsed, bake faces get assembled, an equirect is baked into
        //! a cube. All of them are a compile.
        //!
        //! An environment bake makes three images from one input, so it appends the other
        //! two to `outSubAssets` -- Compile's second output. Every other path leaves it be.
        UniquePtr<AssetData> Compile(const AssetId& id, AssetData& rawData,
                                     eastl::vector<SubAssetEntry>& outSubAssets);

        //! Public for the tests that drive a cube payload without a GPU; production reaches
        //! it through Compile.
        UniquePtr<AssetData> AssembleCubemapData(BakedCubemap&& baked);

        //! Loads the bake shaders, so it must run where the AssetManager can resolve them --
        //! never on the asset worker thread.
        bool InitEnvironmentBaker() { return m_baker.Init(); }

        //! ImageAssetData -> KTX2 bytes, with `identity` stored in the container's key/value
        //! data for the reader to check. 2D and cube, any layer count.
        //! Empty on failure, which the caller reports as "declined to cache".
        eastl::vector<uint8_t> SerializeToKtx2(const ImageAssetData& data,
                                               eastl::string_view identity);

    private:
        UniquePtr<AssetData> CompilePixels(const AssetId& id, ImageAssetRawData& raw);

        //! Equirect pixels -> the sky cube, with irradiance and prefiltered declared as
        //! sub-assets. The bake cannot be split up: its three cubes come out of one GPU job,
        //! where the sky feeds both convolutions as a live SRV.
        UniquePtr<AssetData> CompileEnvironmentCubemap(const AssetId& id,
                                                       const ImageAssetRawData& equirect,
                                                       const ImageAssetDescriptor& desc,
                                                       eastl::vector<SubAssetEntry>& outSubAssets);

        static RHI::Format MapToRHIFormat(ImageFormat src, TextureCompression compression, ImageColorSpace colorSpace);

        EnvironmentBaker m_baker;
    };
}