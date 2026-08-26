#pragma once

#include <Base.h>
#include <Resource/Asset.h>

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
        //! BCn, a container gets parsed, bake faces get assembled. All three are a compile.
        UniquePtr<AssetData> Compile(const AssetId& id, AssetData& rawData);

        //! Public for the tests that drive a cube payload without a GPU; production reaches
        //! it through Compile.
        UniquePtr<AssetData> AssembleCubemapData(BakedCubemap&& baked);

        //! Loads the bake shaders, so it must run where the AssetManager can resolve them --
        //! never on the asset worker thread.
        bool InitEnvironmentBaker() { return m_baker.Init(); }

        //! Equirect pixels -> the three cubes of one bake, still unassembled. Separate from
        //! Compile because its products become three assets, and only the builder registers
        //! assets. Invalid on failure.
        BakedEnvironment BakeEnvironment(const AssetId& id, const ImageAssetRawData& equirect,
                                         const ImageAssetDescriptor& desc);

        //! ImageAssetData -> KTX2 bytes, with `identity` stored in the container's key/value
        //! data for the reader to check. 2D and cube, any layer count.
        //! Empty on failure, which the caller reports as "declined to cache".
        eastl::vector<uint8_t> SerializeToKtx2(const ImageAssetData& data,
                                               eastl::string_view identity);

    private:
        UniquePtr<AssetData> CompilePixels(const AssetId& id, ImageAssetRawData& raw);

        static RHI::Format MapToRHIFormat(ImageFormat src, TextureCompression compression, ImageColorSpace colorSpace);

        EnvironmentBaker m_baker;
    };
}