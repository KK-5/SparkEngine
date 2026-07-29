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

    //! All products of one environment bake. The three cubes share a single HDRI input, a
    //! single GPU job, and a single invalidation moment, so they are produced and returned
    //! together (decision 3). Each is a fully self-contained BakedCubemap — same shape
    //! (6-face, RGBA16F), differing only in faceSize and mipLevels — so no per-product
    //! struct is needed, only this aggregate.
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
        // ctor + dtor are out-of-line (defined where the RHI Ptr members are complete),
        // so value-holding this class (e.g. ImageAssetBuilder::m_baker) does not force
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

        //! Equirect raw (expects RGBAF32) -> the full set of environment products, read
        //! back to CPU. Blocks until the GPU finishes. Currently only the sky cube is
        //! produced (BakedEnvironment::sky); irradiance / prefiltered are added in phase 3d.
        //! Check the specific product you consume (e.g. result.sky.IsValid()).
        BakedEnvironment Bake(const ImageAssetRawData& equirect, uint32_t faceSize);

        //! Recommended cube face resolution for an equirect source of the given height:
        //! equatorial texel density matches at ~H/2 (== W/4 for a 2:1 source), rounded to
        //! the nearest power of two and clamped to [256, 2048]. Used when a cubemap
        //! descriptor requests auto face size (cubemapFaceSize == 0).
        static uint32_t RecommendedFaceSize(uint32_t equirectHeight);

    private:
        //! Equirect -> faceSize^3 RGBA16F cubemap with a full mip chain, read back to CPU.
        //! The sky product of a bake; irradiance / prefiltered (added in 3d) consume the
        //! GPU-side cube this produces, so they will move into the same command stream.
        BakedCubemap BakeSky(const ImageAssetRawData& equirect, uint32_t faceSize);

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
}
