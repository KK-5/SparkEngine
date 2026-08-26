#pragma once

#include <EASTL/vector.h>

#include <Base.h>
#include <RHI/Format.h>

namespace Spark::RHI
{
    class Device;
    class Factory;
    class CommandQueue;
    class CommandRecorder;
    class Fence;
    class PipelineState;
    class PipelineLibrary;
    class PipelineLayoutDescriptor;
    class ShaderBindings;
    class ImagePool;
    class BufferPool;
    class Image;
}

namespace Spark::Resource
{
    class ImageAssetRawData;

    //! CPU-side result of a GPU cubemap bake (read back from the GPU). Six faces of
    //! faceSize x faceSize, tightly packed and face-major (face 0 fully, then face 1,
    //! ...). Phase B wraps this into a cube ImageAssetData (arrayLayers = 6).
    struct BakedCubemap
    {
        uint32_t               faceSize = 0;
        uint32_t               mipLevels = 1;
        RHI::Format            format   = RHI::Format::R16G16B16A16_FLOAT;
        eastl::vector<uint8_t> faceBytes;   // 6 * faceSize * faceSize * 8 bytes

        bool IsValid() const { return faceSize > 0 && !faceBytes.empty(); }
    };

    //! All products of one environment bake — same HDRI input, GPU job, and invalidation,
    //! so produced and returned together. Each is a self-contained BakedCubemap.
    struct BakedEnvironment
    {
        BakedCubemap sky;          //!< full mip chain; skybox sampling + prefilter's sampling source
        BakedCubemap irradiance;   //!< 32^2 single mip; diffuse term
        BakedCubemap prefiltered;  //!< mip N == roughness N/(mipLevels-1); specular term

        bool IsValid() const
        {
            return sky.IsValid() && irradiance.IsValid() && prefiltered.IsValid();
        }
    };

    //! Equirectangular HDRI -> cubemap, on the COMPUTE pipeline, as an asset-processing
    //! step. A self-contained, blocking GPU job: it owns its own PSO / queue / recorder /
    //! fence, records upload + dispatch + readback, submits, and CPU-waits for the result.
    //!
    //! Lives in the asset layer (links SparkRHI directly), so it never touches the render
    //! graph or the per-frame loop — exactly the AsyncUploadSystem precedent for off-frame
    //! GPU work, but on the compute pipeline.
    class EnvironmentBaker
    {
    public:
        //! Shape of the two derived IBL products. Public because ImageAsset's descriptors
        //! reference them -- a descriptor and a bake disagreeing would fail nowhere.
        static constexpr uint32_t kIrradianceSize = 32; //!< low-freq diffuse cube, single mip

        //! 5 roughness levels (0 / .25 / .5 / .75 / 1), not a full chain -- roughness ~1 is
        //! near-uniform, so extra levels only waste bake time. The face size is set by the
        //! mirror end: mip 0 is what a roughness-0 surface reflects, and 256^2 over a 90
        //! degree face is 0.35 degrees per texel.
        //!
        //! A CEILING, not the size: the real one is min(this, sky cube face size), see
        //! PrefilterFaceSize. The sky cube is the information ceiling, so a prefiltered cube
        //! larger than it would only hold a magnified copy -- more memory and bake time for
        //! no extra detail.
        static constexpr uint32_t kPrefilterSizeMax = 256;
        static constexpr uint32_t kPrefilterMips    = 5;

        //! Actual prefiltered face size for a given sky cube face size.
        static constexpr uint32_t PrefilterFaceSize(uint32_t skyFaceSize)
        {
            return skyFaceSize < kPrefilterSizeMax ? skyFaceSize : kPrefilterSizeMax;
        }

        //! Roughness stored at a given mip: 1 - sqrt(1 - mip/(mipCount-1)). Deliberately
        //! NOT uniform -- the levels bunch up at low roughness, because the runtime blends
        //! linearly between two neighbouring convolutions and halfway between a mirror and
        //! a blurred level reads as a sharp image plus a halo, not an intermediate blur.
        //! The same spacing also matches the mip chain's geometric resolution drop against
        //! the GGX lobe's quadratic growth. Endpoints are unchanged: mip 0 is still a
        //! perfect mirror, the last mip still roughness 1.
        //!
        //! MUST remain the exact inverse of RoughnessToLod in Shaders/SceneBindings.hlsl.
        //! Those are two independent copies in two languages; if they drift apart nothing
        //! fails -- every reflection simply comes out uniformly too sharp or too blurred.
        static float LodToRoughness(uint32_t mip, uint32_t mipCount);

        //! Mirror of the HLSL RoughnessToLod (Shaders/SceneBindings.hlsl), which is the one
        //! the renderer actually uses. Has NO production caller here and is not dead code:
        //! it exists so the round trip against LodToRoughness can be unit-tested, which is
        //! the only automated guard on that inverse relationship.
        static float RoughnessToLod(float perceptualRoughness, uint32_t mipCount);

        // ctor + dtor are out-of-line (defined where the RHI Ptr members are complete),
        // so value-holding this class (e.g. ImageAssetCompiler::m_baker) does not force
        // callers to see the full RHI types.
        EnvironmentBaker();
        ~EnvironmentBaker();

        EnvironmentBaker(const EnvironmentBaker&) = delete;
        EnvironmentBaker& operator=(const EnvironmentBaker&) = delete;

        //! One-time setup: builds the compute PSO (reflecting the bake shader for its
        //! layout) + ShaderBindings + queue / recorder / fence. Loads the bake shader,
        //! so it must run where the AssetManager can resolve it (NOT on the asset worker
        //! thread). Returns false on any failure; the baker then stays unusable.
        bool Init();

        bool IsInitialized() const { return m_initialized; }

        //! Equirect raw (expects RGBAF32) -> sky + irradiance + prefiltered cubes, read back
        //! to CPU. Blocks until the GPU finishes.
        BakedEnvironment Bake(const ImageAssetRawData& equirect, uint32_t faceSize);

        //! Recommended cube face resolution for an equirect source of the given height:
        //! equatorial texel density matches at ~H/2 (== W/4 for a 2:1 source), rounded to
        //! the nearest power of two and clamped to [256, 2048]. Used when a cubemap
        //! descriptor requests auto face size (cubemapFaceSize == 0).
        static uint32_t RecommendedFaceSize(uint32_t equirectHeight);

    private:
        //! Equirect -> faceSize^3 RGBA16F cube with a full mip chain. Hands the live GPU
        //! cube back in `outCube` (CopyRead) so the convolutions can sample it as an SRV
        //! without a CPU round-trip; `outMipLevels` is its chain length.
        BakedCubemap BakeSky(const ImageAssetRawData& equirect, uint32_t faceSize,
                             Ptr<RHI::Image>& outCube, uint32_t& outMipLevels);

        //! Sky cube -> diffuse irradiance cube (kIrradianceSize^2, single mip). `srcCube`
        //! must be a live GPU cube from BakeSky, sampled as a TextureCube SRV.
        BakedCubemap BakeIrradiance(RHI::Image& srcCube, uint32_t srcMipLevels);

        //! Sky cube -> GGX-prefiltered specular cube (PrefilterFaceSize^2, kPrefilterMips), one
        //! roughness per mip. `srcCube` must be a live GPU cube from BakeSky.
        BakedCubemap BakePrefilter(RHI::Image& srcCube, uint32_t srcMipLevels);

        bool m_initialized = false;

        RHI::Device*  m_device  = nullptr;
        RHI::Factory* m_factory = nullptr;

        Ptr<RHI::PipelineLibrary>          m_pipelineLibrary;
        Ptr<RHI::PipelineLayoutDescriptor> m_layout;
        Ptr<RHI::PipelineState>            m_pso;
        Ptr<RHI::CommandQueue>             m_queue;
        Ptr<RHI::CommandRecorder>          m_recorder;
        Ptr<RHI::Fence>                    m_fence;

        Ptr<RHI::PipelineState>            m_cubeMipsPSO;
        Ptr<RHI::PipelineLayoutDescriptor> m_cubeMipsLayout;

        Ptr<RHI::PipelineState>            m_irradiancePSO;
        Ptr<RHI::PipelineLayoutDescriptor> m_irradianceLayout;

        Ptr<RHI::PipelineState>            m_prefilterPSO;
        Ptr<RHI::PipelineLayoutDescriptor> m_prefilterLayout;

        // Persistent resource allocators — the pools (not the factory) own the heaps
        // and deferred-release queues, so they must outlive every image/buffer they
        // hand out. Created once in Init, reused across bakes. equirect (SRV) and cube
        // (UAV) share one image pool (union of bind flags); upload vs readback need
        // different heap types (UPLOAD vs READBACK), so two separate buffer pools.
        Ptr<RHI::ImagePool>                m_imagePool;
        Ptr<RHI::BufferPool>               m_stagingPool;
        Ptr<RHI::BufferPool>               m_readbackPool;

        // Destory before ImagePool/BufferPool
        Ptr<RHI::ShaderBindings>           m_bindings;
    };

    // The cap only bites once kPrefilterSizeMax rises above RecommendedFaceSize's 256 floor,
    // so no bake exercises it today. Pin the rule itself instead of waiting for that.
    static_assert(EnvironmentBaker::PrefilterFaceSize(128) == 128,
        "a sky cube smaller than the cap must cap the prefiltered cube to itself");
    static_assert(EnvironmentBaker::PrefilterFaceSize(EnvironmentBaker::kPrefilterSizeMax)
        == EnvironmentBaker::kPrefilterSizeMax, "equal sizes must pass through");
    static_assert(EnvironmentBaker::PrefilterFaceSize(2048) == EnvironmentBaker::kPrefilterSizeMax,
        "a larger sky cube must not raise the prefiltered cube above the cap");
}
