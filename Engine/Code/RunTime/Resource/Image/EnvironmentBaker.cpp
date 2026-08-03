#include "EnvironmentBaker.h"

#include <EASTL/array.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/RHILimits.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandRecorder.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/DispatchItem.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>
#include <RHI/Resource/ShaderInput/ShaderInputCompiler.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>
#include <Resource/Image/ImageAsset.h>

namespace Spark::Resource
{
    namespace
    {
        constexpr RHI::Format kCubeFormat   = RHI::Format::R16G16B16A16_FLOAT;
        constexpr uint32_t    kCubeBytesPP  = 8; // RGBA16F
        constexpr uint32_t    kNumCubeFaces = 6;
        constexpr uint32_t    kPrefilterSamples = 1024;

        RHI::Format MapRawToRHIFormat(ImageFormat format)
        {
            switch (format)
            {
            case ImageFormat::RGBAF32: return RHI::Format::R32G32B32A32_FLOAT;
            case ImageFormat::RGBA8:   return RHI::Format::R8G8B8A8_UNORM;
            case ImageFormat::RG8:     return RHI::Format::R8G8_UNORM;
            case ImageFormat::R8:      return RHI::Format::R8_UNORM;
            default:                   return RHI::Format::Unknown;
            }
        }
    }

    EnvironmentBaker::EnvironmentBaker() = default;
    EnvironmentBaker::~EnvironmentBaker() = default;

    uint32_t EnvironmentBaker::RecommendedFaceSize(uint32_t equirectHeight)
    {
        // Equirect is 2:1; a cube face covers 90°, so equatorial density matches at ~H/2.
        uint32_t target = equirectHeight / 2;
        if (target < 1)
        {
            target = 1;
        }

        // Round to the nearest power of two.
        uint32_t lo = 1;
        while ((lo << 1) <= target)
        {
            lo <<= 1;
        }
        const uint32_t hi  = lo << 1;
        uint32_t       pot = (target - lo <= hi - target) ? lo : hi;

        // Clamp to a sane range.
        constexpr uint32_t kMinFace = 256;
        constexpr uint32_t kMaxFace = 2048;
        if (pot < kMinFace)
        {
            pot = kMinFace;
        }
        if (pot > kMaxFace)
        {
            pot = kMaxFace;
        }
        return pot;
    }

    bool EnvironmentBaker::Init()
    {
        if (m_initialized)
        {
            return true;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        if (!rhi)
        {
            LOG_ERROR("[EnvironmentBaker] RHIInterface service missing.");
            return false;
        }
        m_factory = rhi->GetRHIFactory();
        m_device  = rhi->GetDevice();
        if (!m_factory || !m_device)
        {
            LOG_ERROR("[EnvironmentBaker] RHI factory or device not ready.");
            return false;
        }

        // Load + reflect the bake compute shader (CSMain -> cs_6_0, auto-detected).
        auto* assetManager = Service<AssetManager>::Get();
        if (!assetManager)
        {
            LOG_ERROR("[EnvironmentBaker] AssetManager service missing.");
            return false;
        }
        Ptr<ShaderAsset> shader = assetManager->LoadAsset<ShaderAsset>(
            AssetId::Of<ShaderAsset>("Shaders/Image/EnvironmentBake.hlsl"));
        if (!shader || shader->GetStatus() != AssetStatus::Ready)
        {
            LOG_ERROR("[EnvironmentBaker] Failed to load EnvironmentBake.hlsl (status={}).",
                      shader ? static_cast<int>(shader->GetStatus()) : -1);
            return false;
        }

        // Reflected layout (asset-layer BuildShaderInputList) -> PipelineLayoutDescriptor.
        ShaderInputBuildResult built = BuildShaderInputList(*shader);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[EnvironmentBaker] Bake shader produced no shader inputs.");
            return false;
        }
        m_layout = m_factory->CreatePipelineLayoutDescriptor();
        m_layout->AddShaderInputDescriptors(built.list, built.stageMask);
        m_layout->Finalize();

        // ShaderBindings for space0 (equirect SRV + sampler + cube UAV live there).
        m_bindings = m_factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor bindingsDesc;
        bindingsDesc.m_layout  = m_layout;
        bindingsDesc.m_spaceId = 0;
        if (m_bindings->Init(*m_device, bindingsDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[EnvironmentBaker] ShaderBindings init failed.");
            return false;
        }

        // Compute PSO.
        m_pipelineLibrary = m_factory->CreatePipelineLibrary();
        m_pipelineLibrary->Init(*m_device, RHI::PipelineLibraryDescriptor{});

        auto* shaderData = shader->GetShaderData();
        const auto* csBytecode = shaderData
            ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
            : nullptr;
        if (!csBytecode)
        {
            LOG_ERROR("[EnvironmentBaker] Bake shader has no compute-stage bytecode "
                      "(CSMain not detected/compiled?).");
            return false;
        }
        Ptr<RHI::ShaderStageFunction> csFunc =
            m_factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
        csFunc->SetByteCode(csBytecode->bytecode);
        csFunc->Finalize();

        m_pso = m_factory->CreatePipelineState();
        RHI::PipelineStateDescriptorForDispatch psoDesc;
        psoDesc.m_computeFunction          = csFunc;
        psoDesc.m_pipelineLayoutDescriptor = m_layout;
        if (m_pso->Init(*m_device, psoDesc, m_pipelineLibrary.get()) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[EnvironmentBaker] Compute PSO init failed.");
            return false;
        }
        /////////////////////////////////////////////////////////////////
        // Cube mips PSO
        {
            Ptr<ShaderAsset> cubeMapDownsampleShader = assetManager->LoadAsset<ShaderAsset>(
                AssetId::Of<ShaderAsset>("Shaders/Image/CubemapDownsample.hlsl"));
            if (!cubeMapDownsampleShader || cubeMapDownsampleShader->GetStatus() != AssetStatus::Ready)
            {
                LOG_ERROR("[EnvironmentBaker] Failed to load CubemapDownsample.hlsl (status={}).",
                        cubeMapDownsampleShader ? static_cast<int>(cubeMapDownsampleShader->GetStatus()) : -1);
                return false;
            }

            auto* shaderData = cubeMapDownsampleShader->GetShaderData();
            const auto* csBytecode = shaderData
                ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
                : nullptr;
            if (!csBytecode)
            {
                LOG_ERROR("[EnvironmentBaker] Cube map down sample shader has no compute-stage bytecode "
                        "(CSMain not detected/compiled?).");
                return false;
            }
            Ptr<RHI::ShaderStageFunction> csFunc =
                m_factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
            csFunc->SetByteCode(csBytecode->bytecode);
            csFunc->Finalize();

            ShaderInputBuildResult built = BuildShaderInputList(*cubeMapDownsampleShader);
            if (built.stageMask == RHI::ShaderStageMask::None)
            {
                LOG_ERROR("[EnvironmentBaker] Bake shader produced no shader inputs.");
                return false;
            }
            m_cubeMipsLayout = m_factory->CreatePipelineLayoutDescriptor();
            m_cubeMipsLayout->AddShaderInputDescriptors(built.list, built.stageMask);
            m_cubeMipsLayout->Finalize();

            m_cubeMipsPSO = m_factory->CreatePipelineState();
            RHI::PipelineStateDescriptorForDispatch psoDesc;
            psoDesc.m_computeFunction          = csFunc;
            psoDesc.m_pipelineLayoutDescriptor = m_cubeMipsLayout;
            if (m_cubeMipsPSO->Init(*m_device, psoDesc, m_pipelineLibrary.get()) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] Compute PSO init failed.");
                return false;
            }
        }
        /////////////////////////////////////////////////////////////////
        // Irradiance PSO (cosine-weighted diffuse convolution of the sky cube)
        {
            Ptr<ShaderAsset> irradianceShader = assetManager->LoadAsset<ShaderAsset>(
                AssetId::Of<ShaderAsset>("Shaders/Image/IrradianceBake.hlsl"));
            if (!irradianceShader || irradianceShader->GetStatus() != AssetStatus::Ready)
            {
                LOG_ERROR("[EnvironmentBaker] Failed to load IrradianceBake.hlsl (status={}).",
                        irradianceShader ? static_cast<int>(irradianceShader->GetStatus()) : -1);
                return false;
            }

            auto* shaderData = irradianceShader->GetShaderData();
            const auto* csBytecode = shaderData
                ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
                : nullptr;
            if (!csBytecode)
            {
                LOG_ERROR("[EnvironmentBaker] Irradiance shader has no compute-stage bytecode "
                        "(CSMain not detected/compiled?).");
                return false;
            }
            Ptr<RHI::ShaderStageFunction> csFunc =
                m_factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
            csFunc->SetByteCode(csBytecode->bytecode);
            csFunc->Finalize();

            ShaderInputBuildResult built = BuildShaderInputList(*irradianceShader);
            if (built.stageMask == RHI::ShaderStageMask::None)
            {
                LOG_ERROR("[EnvironmentBaker] Irradiance shader produced no shader inputs.");
                return false;
            }
            m_irradianceLayout = m_factory->CreatePipelineLayoutDescriptor();
            m_irradianceLayout->AddShaderInputDescriptors(built.list, built.stageMask);
            m_irradianceLayout->Finalize();

            m_irradiancePSO = m_factory->CreatePipelineState();
            RHI::PipelineStateDescriptorForDispatch psoDesc;
            psoDesc.m_computeFunction          = csFunc;
            psoDesc.m_pipelineLayoutDescriptor = m_irradianceLayout;
            if (m_irradiancePSO->Init(*m_device, psoDesc, m_pipelineLibrary.get()) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] Irradiance PSO init failed.");
                return false;
            }
        }
        /////////////////////////////////////////////////////////////////
        // Prefilter PSO (GGX-prefiltered specular convolution of the sky cube)
        {
            Ptr<ShaderAsset> prefilterShader = assetManager->LoadAsset<ShaderAsset>(
                AssetId::Of<ShaderAsset>("Shaders/Image/PrefilterBake.hlsl"));
            if (!prefilterShader || prefilterShader->GetStatus() != AssetStatus::Ready)
            {
                LOG_ERROR("[EnvironmentBaker] Failed to load PrefilterBake.hlsl (status={}).",
                        prefilterShader ? static_cast<int>(prefilterShader->GetStatus()) : -1);
                return false;
            }

            auto* shaderData = prefilterShader->GetShaderData();
            const auto* csBytecode = shaderData
                ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
                : nullptr;
            if (!csBytecode)
            {
                LOG_ERROR("[EnvironmentBaker] Prefilter shader has no compute-stage bytecode "
                        "(CSMain not detected/compiled?).");
                return false;
            }
            Ptr<RHI::ShaderStageFunction> csFunc =
                m_factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
            csFunc->SetByteCode(csBytecode->bytecode);
            csFunc->Finalize();

            ShaderInputBuildResult built = BuildShaderInputList(*prefilterShader);
            if (built.stageMask == RHI::ShaderStageMask::None)
            {
                LOG_ERROR("[EnvironmentBaker] Prefilter shader produced no shader inputs.");
                return false;
            }
            m_prefilterLayout = m_factory->CreatePipelineLayoutDescriptor();
            m_prefilterLayout->AddShaderInputDescriptors(built.list, built.stageMask);
            m_prefilterLayout->Finalize();

            m_prefilterPSO = m_factory->CreatePipelineState();
            RHI::PipelineStateDescriptorForDispatch psoDesc;
            psoDesc.m_computeFunction          = csFunc;
            psoDesc.m_pipelineLayoutDescriptor = m_prefilterLayout;
            if (m_prefilterPSO->Init(*m_device, psoDesc, m_pipelineLibrary.get()) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] Prefilter PSO init failed.");
                return false;
            }
        }
        /////////////////////////////////////////////////////////////////

        // Own dedicated graphics queue + recorder + fence. Graphics queue runs the
        // compute dispatch + copies with no queue-class restrictions; the compute
        // PIPELINE is what keeps this off the raster path. Blocking one-shot, so
        // it does not meaningfully contend with the frame.
        m_queue = m_factory->CreateCommandQueue();
        RHI::CommandQueueDescriptor queueDesc;
        queueDesc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        queueDesc.m_maxFrameQueueDepth = 1;
        if (m_queue->Init(*m_device, queueDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[EnvironmentBaker] Command queue init failed.");
            return false;
        }

        m_recorder = m_factory->CreateCommandRecorder();
        RHI::CommandRecorderDescriptor recorderDesc;
        recorderDesc.m_queue = RHI::HardwareQueueClass::Graphics;
        if (m_recorder->Init(*m_device, recorderDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[EnvironmentBaker] Command recorder init failed.");
            return false;
        }

        m_fence = m_factory->CreateFence();
        if (m_fence->Init(*m_device, RHI::FenceState::Reset) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[EnvironmentBaker] Fence init failed.");
            return false;
        }

        // Persistent pools (see header): one image pool for equirect (SRV) + cube (UAV)
        // with the union of bind flags; separate upload / readback buffer pools because
        // their heap types differ.
        m_imagePool = m_factory->CreateImagePool();
        {
            RHI::ImagePoolDescriptor desc;
            desc.m_bindFlags = RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::ShaderWrite
                             | RHI::ImageBindFlags::CopyRead  | RHI::ImageBindFlags::CopyWrite;
            if (m_imagePool->Init(*m_device, desc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] image pool init failed.");
                return false;
            }
        }

        m_stagingPool = m_factory->CreateBufferPool();
        {
            RHI::BufferPoolDescriptor desc;
            desc.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
            desc.m_bindFlags        = RHI::BufferBindFlags::CopyRead;
            desc.m_sharedQueueMask  = RHI::HardwareQueueClassMask::All;
            if (m_stagingPool->Init(*m_device, desc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] staging buffer pool init failed.");
                return false;
            }
        }

        m_readbackPool = m_factory->CreateBufferPool();
        {
            RHI::BufferPoolDescriptor desc;
            desc.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Read;
            desc.m_bindFlags        = RHI::BufferBindFlags::CopyWrite;
            desc.m_sharedQueueMask  = RHI::HardwareQueueClassMask::All;
            if (m_readbackPool->Init(*m_device, desc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] readback buffer pool init failed.");
                return false;
            }
        }

        m_initialized = true;
        return true;
    }

    BakedEnvironment EnvironmentBaker::Bake(const ImageAssetRawData& equirect, uint32_t faceSize)
    {
        BakedEnvironment env;

        // Sky first: it hands back the live GPU cube (with its mip chain) so the follow-up
        // convolutions sample it as an SRV — no CPU round-trip. Bake owns the Ptr, keeping
        // the cube alive across BakeIrradiance / BakePrefilter.
        Ptr<RHI::Image> skyCube;
        uint32_t        skyMipLevels = 0;
        env.sky = BakeSky(equirect, faceSize, skyCube, skyMipLevels);
        if (!env.sky.IsValid() || !skyCube)
        {
            return env;
        }

        env.irradiance  = BakeIrradiance(*skyCube, skyMipLevels);
        env.prefiltered = BakePrefilter(*skyCube, skyMipLevels);
        return env;
    }

    BakedCubemap EnvironmentBaker::BakeSky(const ImageAssetRawData& equirect, uint32_t faceSize,
                                           Ptr<RHI::Image>& outCube, uint32_t& outMipLevels)
    {
        BakedCubemap result;

        if (!m_initialized)
        {
            LOG_ERROR("[EnvironmentBaker] Bake called before Init.");
            return result;
        }
        if (faceSize == 0 || equirect.GetPixels().empty())
        {
            LOG_ERROR("[EnvironmentBaker] Invalid bake input.");
            return result;
        }

        const RHI::Format srcFormat = MapRawToRHIFormat(equirect.GetFormat());
        if (srcFormat == RHI::Format::Unknown)
        {
            LOG_ERROR("[EnvironmentBaker] Unsupported equirect format.");
            return result;
        }

        const uint32_t srcW = equirect.GetWidth();
        const uint32_t srcH = equirect.GetHeight();

        // ---- Per-bake GPU resources (allocated from the persistent member pools;
        //      released when this function returns, after the GPU work has completed) ----
        Ptr<RHI::Image> equirectImg = m_factory->CreateImage();
        {
            RHI::ImageInitRequest req;
            req.m_image = equirectImg.get();
            req.m_descriptor = RHI::ImageDescriptor::Create2D(
                RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
                srcW, srcH, srcFormat);
            req.m_descriptor.m_mipLevels = 1;
            if (m_imagePool->InitImage(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] equirect image init failed.");
                return result;
            }
        }

        Ptr<RHI::Image> cubeImg = m_factory->CreateImage();
        const uint32_t cubeMipLevels = ImageAsset::ComputeMipLevels(faceSize, faceSize, 0);
        {
            RHI::ImageInitRequest req;
            req.m_image = cubeImg.get();
            req.m_descriptor = RHI::ImageDescriptor::CreateCubemap(
                RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::ShaderRead
                | RHI::ImageBindFlags::CopyRead,
                faceSize, kCubeFormat);
            req.m_descriptor.m_mipLevels = cubeMipLevels;
            if (m_imagePool->InitImage(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] cube image init failed.");
                return result;
            }
        }

        // ---- Staging: equirect pixels -> host-visible buffer (aligned row pitch) ----
        const RHI::ImageSubresourceLayout srcLayout =
            RHI::GetImageSubresourceLayout(RHI::Size{srcW, srcH, 1}, srcFormat);
        const uint32_t srcRowBytes    = srcLayout.m_bytesPerRow;
        const uint32_t srcRowCount    = srcLayout.m_rowCount;
        const uint32_t srcRowAligned  = AlignUp(srcRowBytes, RHI::Alignment::TexturePitch);
        const uint32_t stageBytes     = srcRowAligned * srcRowCount;

        Ptr<RHI::Buffer> stageBuf = m_factory->CreateBuffer();
        {
            RHI::BufferDescriptor desc;
            desc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
            desc.m_byteCount = stageBytes;
            RHI::BufferInitRequest req;
            req.m_buffer = stageBuf.get();
            req.m_descriptor = desc;
            if (m_stagingPool->InitBuffer(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] staging buffer init failed.");
                return result;
            }
        }
        {
            RHI::BufferMapRequest mapReq;
            mapReq.m_buffer     = stageBuf.get();
            mapReq.m_byteOffset = 0;
            mapReq.m_byteCount  = stageBytes;
            RHI::BufferMapResponse mapResp;
            if (m_stagingPool->MapBuffer(mapReq, mapResp) != RHI::ResultCode::Success || !mapResp.m_data)
            {
                LOG_ERROR("[EnvironmentBaker] staging buffer map failed.");
                return result;
            }
            RHI::MemoryCopyDest dst;
            dst.pData      = mapResp.m_data;
            dst.rowPitch   = srcRowAligned;
            dst.slicePitch = stageBytes;
            RHI::MemoryCopySrc src;
            src.pData      = const_cast<uint8_t*>(equirect.GetPixels().data());
            src.rowPitch   = srcRowBytes;
            src.slicePitch = srcLayout.m_bytesPerImage;
            m_stagingPool->MemcpySubresource(&dst, &src, srcRowBytes, srcRowCount, 1);
            m_stagingPool->UnmapBuffer(*stageBuf);
        }

        // ---- Views: equirect SRV + cube UAV (Texture2DArray over the 6 faces) ----
        Ptr<RHI::ImageView> equirectView = m_factory->CreateImageView();
        {
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin   = 0;
            viewDesc.m_mipSliceMax   = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            viewDesc.m_overrideFormat = srcFormat;
            if (equirectView->Init(*equirectImg, viewDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] equirect view init failed.");
                return result;
            }
        }
        Ptr<RHI::ImageView> cubeView = m_factory->CreateImageView();
        {
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin   = 0;
            viewDesc.m_mipSliceMax   = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = static_cast<uint16_t>(kNumCubeFaces - 1);
            viewDesc.m_overrideFormat = kCubeFormat;
            if (cubeView->Init(*cubeImg, viewDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] cube view init failed.");
                return result;
            }
        }

        eastl::vector<Ptr<RHI::ImageView>> cubeMipViews;
        cubeMipViews.resize(cubeMipLevels);
        for (uint32_t mip = 0; mip < cubeMipLevels; ++mip)
        {
            cubeMipViews[mip] = m_factory->CreateImageView();
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin   = static_cast<uint16_t>(mip);
            viewDesc.m_mipSliceMax   = static_cast<uint16_t>(mip);
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = static_cast<uint16_t>(kNumCubeFaces - 1);
            viewDesc.m_overrideFormat = kCubeFormat;
            if (cubeMipViews[mip]->Init(*cubeImg, viewDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] cube view init failed.");
                return result;
            }
        }

        eastl::vector<Ptr<RHI::ShaderBindings>> cubeMipBindings;
        cubeMipBindings.resize(cubeMipLevels - 1);
        for (auto& bindings: cubeMipBindings)
        {
            bindings = m_factory->CreateShaderBindings();
            RHI::ShaderBindings::Descriptor bindingsDesc;
            bindingsDesc.m_layout  = m_cubeMipsLayout;
            bindingsDesc.m_spaceId = 0;
            if (bindings->Init(*m_device, bindingsDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] ShaderBindings init failed.");
                return result;
            }
        }

        // ---- Bind + compile ShaderBindings ----
        if (auto* in = m_bindings->FindImageInput(RHI::InputName("g_Equirect")))
        {
            in->SetView(0, equirectView.get());
        }
        if (auto* in = m_bindings->FindSamplerInput(RHI::InputName("g_Sampler")))
        {
            in->SetState(0, RHI::SamplerState::Create(
                RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));
        }
        if (auto* in = m_bindings->FindImageInput(RHI::InputName("g_Cube")))
        {
            in->SetView(0, cubeView.get());
        }
        {
            RHI::ShaderInputCompiler& compiler = m_factory->AcquireShaderInputCompiler(*m_device);
            compiler.Compile(*m_bindings);
        }

        {
            for (uint32_t mip = 0; mip < cubeMipLevels - 1; ++mip)
            {
                if (auto* in = cubeMipBindings[mip]->FindImageInput(RHI::InputName("g_SrcMip")))
                {
                    in->SetView(0, cubeMipViews[mip].get());
                }
                if (auto* in = cubeMipBindings[mip]->FindImageInput(RHI::InputName("g_DstMip")))
                {
                    in->SetView(0, cubeMipViews[mip + 1].get());
                }

                RHI::ShaderInputCompiler& compiler = m_factory->AcquireShaderInputCompiler(*m_device);
                compiler.Compile(*cubeMipBindings[mip]);
            }
        }

        // ---- Readback buffers: one per (face, mip), each at offset 0 (always aligned) ----
        // GetSubresourceLayouts writes by GLOBAL subresource index (mip + array*mipLevels),
        // so the output must span the full mip*face grid and every lookup goes through
        // GetImageSubresourceIndex rather than assuming index == face.
        eastl::vector<RHI::ImageSubresourceLayout> subresLayouts(
            static_cast<size_t>(cubeMipLevels) * kNumCubeFaces);
        cubeImg->GetSubresourceLayouts(
            RHI::ImageSubresourceRange(0, static_cast<uint16_t>(cubeMipLevels - 1), 0, static_cast<uint16_t>(kNumCubeFaces - 1)),
            subresLayouts.data(), nullptr);

        // GPU-side (row-aligned) layout of cube face f at mip level `mip`.
        auto FaceMipLayout = [&](uint32_t f, uint32_t mip) -> const RHI::ImageSubresourceLayout&
        {
            return subresLayouts[RHI::GetImageSubresourceIndex(
                static_cast<uint16_t>(mip),
                static_cast<uint16_t>(f),
                static_cast<uint16_t>(cubeMipLevels))];
        };

        // Linear index into readbackBufs / the face-major, mip-inner byte order.
        auto SubresIndex = [&](uint32_t f, uint32_t mip) -> size_t
        {
            return static_cast<size_t>(f) * cubeMipLevels + mip;
        };

        eastl::vector<Ptr<RHI::Buffer>> readbackBufs(static_cast<size_t>(kNumCubeFaces) * cubeMipLevels);
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            for (uint32_t mip = 0; mip < cubeMipLevels; ++mip)
            {
                const size_t idx = SubresIndex(f, mip);
                readbackBufs[idx] = m_factory->CreateBuffer();
                RHI::BufferDescriptor desc;
                desc.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
                desc.m_byteCount = FaceMipLayout(f, mip).m_bytesPerImage;
                RHI::BufferInitRequest req;
                req.m_buffer = readbackBufs[idx].get();
                req.m_descriptor = desc;
                if (m_readbackPool->InitBuffer(req) != RHI::ResultCode::Success)
                {
                    LOG_ERROR("[EnvironmentBaker] readback buffer init failed.");
                    return result;
                }
            }
        }

        // ---- Record: upload -> dispatch -> readback ----
        RHI::CommandList* cmd = m_recorder->GetCommandList();

        RHI::BufferBarrier stageBarrier = RHI::ConvertToCopyRead(*stageBuf);
        cmd->QueueBarrier(stageBarrier);
        RHI::ImageBarrier equirectCopyBarrier = RHI::ConvertToImageCopyWrite(*equirectImg);
        cmd->QueueBarrier(equirectCopyBarrier);
        cmd->FlushBarriers();

        // Upload equirect.
        {
            RHI::CopyBufferToImageDescriptor up;
            up.m_sourceBuffer         = stageBuf.get();
            up.m_sourceOffset         = 0;
            up.m_sourceBytesPerRow    = srcRowAligned;
            up.m_sourceBytesPerImage  = stageBytes;
            up.m_sourceFormat         = srcFormat;
            up.m_sourceSize           = RHI::Size{srcW, srcH, 1};
            up.m_destinationImage     = equirectImg.get();
            up.m_destinationSubresource = RHI::ImageSubresource(0, 0);
            up.m_destinationOrigin    = RHI::Origin(0, 0, 0);
            cmd->Submit(RHI::CopyItem(up));
        }

        // equirect -> shader read, cube -> shader write (UAV).
        RHI::ImageBarrier equirectReadBarrier = RHI::ConvertToImageShaderRead(*equirectImg);
        cmd->QueueBarrier(equirectReadBarrier);
        RHI::ImageBarrier cubeWriteBarrier = RHI::ConvertToImageShaderWrite(*cubeImg);
        cmd->QueueBarrier(cubeWriteBarrier);
        cmd->FlushBarriers();

        cmd->SetPipelineState(*m_pso);
        cmd->BindShaderInputsForDispatch(*m_bindings);
        {
            RHI::DispatchItem di;
            di.m_arguments = RHI::DispatchArguments(
                RHI::DispatchDirect(faceSize, faceSize, kNumCubeFaces, 8, 8, 1));
            di.m_pipelineState = m_pso.get();
            cmd->Submit(di);
        }

        cmd->SetPipelineState(*m_cubeMipsPSO);
        for (uint32_t mip = 0; mip < cubeMipLevels - 1; ++mip)
        {
            RHI::ImageBarrier uavBarrier = RHI::ConvertToImageShaderWrite(*cubeImg);
            cmd->QueueBarrier(uavBarrier);
            cmd->FlushBarriers();

            cmd->BindShaderInputsForDispatch(*cubeMipBindings[mip]);

            const uint32_t dstSize = eastl::max(1u, faceSize >> (mip + 1));
            RHI::DispatchItem di;
            di.m_arguments = RHI::DispatchArguments(
                RHI::DispatchDirect(dstSize, dstSize, kNumCubeFaces, 8, 8, 1));
            di.m_pipelineState = m_cubeMipsPSO.get();
            cmd->Submit(di);
        }

        // cube -> copy read, then read each face back.
        RHI::ImageBarrier cubeReadBarrier = RHI::ConvertToImageCopyRead(*cubeImg);
        cmd->QueueBarrier(cubeReadBarrier);
        cmd->FlushBarriers();

        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            for (uint32_t mip = 0; mip < cubeMipLevels; ++mip)
            {
                RHI::CopyImageToBufferDescriptor rb;
                rb.m_sourceImage            = cubeImg.get();
                rb.m_sourceSubresource      = RHI::ImageSubresource(
                    static_cast<uint16_t>(mip), static_cast<uint16_t>(f));
                rb.m_sourceOrigin           = RHI::Origin(0, 0, 0);
                rb.m_sourceSize             = FaceMipLayout(f, mip).m_size;
                rb.m_destinationBuffer      = readbackBufs[SubresIndex(f, mip)].get();
                rb.m_destinationOffset      = 0;
                rb.m_destinationBytesPerRow = FaceMipLayout(f, mip).m_bytesPerRow;
                rb.m_destinationBytesPerImage = FaceMipLayout(f, mip).m_bytesPerImage;
                rb.m_destinationFormat      = kCubeFormat;
                cmd->Submit(RHI::CopyItem(rb));
            }
        }

        cmd->Close();

        // ---- Submit + block ----
        // FlushCommands = Reset (advances the fence's pending value via Increment) ->
        // Signal -> WaitOnCpu. The Reset is essential for reuse: Signal() alone signals
        // to the CURRENT pending value and does not advance it, so a second bake would
        // wait on a value the fence already reached and return without waiting for the GPU.
        RHI::CommandList* lists[] = { cmd };
        m_queue->ExecuteCommands(lists);
        m_queue->FlushCommands(*m_fence);

        m_recorder->Reset();

        // ---- Read back every subresource, de-pad rows into a tight buffer ----
        // Byte order is FACE-MAJOR, MIP-INNER: [f0m0][f0m1]...[f0mN][f1m0]... This is a
        // hard contract with AsyncUploadSystem, which walks arraySlice outer / mipSlice
        // inner and advances srcData by each subresource's TIGHT byte count -- it never
        // reads an explicit offset table. Swapping these two loops still compiles, still
        // runs, and silently produces face/mip-scrambled output.
        result.faceSize  = faceSize;
        result.mipLevels = cubeMipLevels;
        result.format    = kCubeFormat;
        size_t totalBytes = 0;
        for (uint32_t mip = 0; mip < cubeMipLevels; ++mip)
        {
            const uint32_t s = eastl::max(1u, faceSize >> mip);
            totalBytes += static_cast<size_t>(s) * s * kCubeBytesPP;
        }
        totalBytes *= kNumCubeFaces;
        result.faceBytes.resize(totalBytes);

        size_t dstOffset = 0;
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            for (uint32_t mip = 0; mip < cubeMipLevels; ++mip)
            {
                const auto&    layout   = FaceMipLayout(f, mip);
                const size_t   idx      = SubresIndex(f, mip);
                const uint32_t mipSize  = eastl::max(1u, faceSize >> mip);
                const uint32_t tightRow = mipSize * kCubeBytesPP;

                RHI::BufferMapRequest mapReq;
                mapReq.m_buffer     = readbackBufs[idx].get();
                mapReq.m_byteOffset = 0;
                mapReq.m_byteCount  = layout.m_bytesPerImage;
                RHI::BufferMapResponse mapResp;
                if (m_readbackPool->MapBuffer(mapReq, mapResp) != RHI::ResultCode::Success || !mapResp.m_data)
                {
                    LOG_ERROR("[EnvironmentBaker] readback map failed.");
                    return BakedCubemap{};
                }

                const auto* srcBytes = static_cast<const uint8_t*>(mapResp.m_data);
                for (uint32_t row = 0; row < layout.m_rowCount; ++row)
                {
                    memcpy(result.faceBytes.data() + dstOffset + row * tightRow,
                        srcBytes + static_cast<size_t>(row) * layout.m_bytesPerRow,
                        tightRow);
                }
                m_readbackPool->UnmapBuffer(*readbackBufs[idx]);

                dstOffset += static_cast<size_t>(tightRow) * mipSize;
            }
        }

        // Keep the GPU cube alive for the follow-up convolutions instead of releasing it.
        outCube      = cubeImg;
        outMipLevels = cubeMipLevels;
        return result;
    }

    BakedCubemap EnvironmentBaker::BakeIrradiance(RHI::Image& srcCube, uint32_t srcMipLevels)
    {
        BakedCubemap result;

        // ---- Irradiance cube (UAV target) + views ----
        Ptr<RHI::Image> irrImg = m_factory->CreateImage();
        {
            RHI::ImageInitRequest req;
            req.m_image = irrImg.get();
            req.m_descriptor = RHI::ImageDescriptor::CreateCubemap(
                RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead,
                kIrradianceSize, kCubeFormat);
            req.m_descriptor.m_mipLevels = 1;
            if (m_imagePool->InitImage(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] irradiance image init failed.");
                return result;
            }
        }

        // Sky cube as a TextureCube SRV over its full mip chain.
        Ptr<RHI::ImageView> srcView = m_factory->CreateImageView();
        {
            RHI::ImageViewDescriptor viewDesc = RHI::ImageViewDescriptor::CreateCubemap(
                kCubeFormat, 0, static_cast<uint16_t>(srcMipLevels - 1));
            if (srcView->Init(srcCube, viewDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] irradiance source view init failed.");
                return result;
            }
        }

        // Destination UAV: array-slice view, NOT CreateCubemap (a UAV sees raw array slices).
        Ptr<RHI::ImageView> irrView = m_factory->CreateImageView();
        {
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin    = 0;
            viewDesc.m_mipSliceMax    = 0;
            viewDesc.m_arraySliceMin  = 0;
            viewDesc.m_arraySliceMax  = static_cast<uint16_t>(kNumCubeFaces - 1);
            viewDesc.m_overrideFormat = kCubeFormat;
            if (irrView->Init(*irrImg, viewDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] irradiance dest view init failed.");
                return result;
            }
        }

        // ---- Bindings ----
        Ptr<RHI::ShaderBindings> bindings = m_factory->CreateShaderBindings();
        {
            RHI::ShaderBindings::Descriptor bindingsDesc;
            bindingsDesc.m_layout  = m_irradianceLayout;
            bindingsDesc.m_spaceId = 0;
            if (bindings->Init(*m_device, bindingsDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] irradiance ShaderBindings init failed.");
                return result;
            }
        }
        if (auto* in = bindings->FindImageInput(RHI::InputName("g_SrcCube")))
        {
            in->SetView(0, srcView.get());
        }
        if (auto* in = bindings->FindSamplerInput(RHI::InputName("g_Sampler")))
        {
            // Trilinear for the shader's fractional srcLod; Clamp.
            in->SetState(0, RHI::SamplerState::Create(
                RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));
        }
        if (auto* in = bindings->FindImageInput(RHI::InputName("g_Irradiance")))
        {
            in->SetView(0, irrView.get());
        }
        {
            RHI::ShaderInputCompiler& compiler = m_factory->AcquireShaderInputCompiler(*m_device);
            compiler.Compile(*bindings);
        }

        // ---- Readback buffers: one per face (single mip) ----
        eastl::vector<RHI::ImageSubresourceLayout> subresLayouts(kNumCubeFaces);
        irrImg->GetSubresourceLayouts(
            RHI::ImageSubresourceRange(0, 0, 0, static_cast<uint16_t>(kNumCubeFaces - 1)),
            subresLayouts.data(), nullptr);

        eastl::vector<Ptr<RHI::Buffer>> readbackBufs(kNumCubeFaces);
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            readbackBufs[f] = m_factory->CreateBuffer();
            RHI::BufferDescriptor desc;
            desc.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
            desc.m_byteCount = subresLayouts[f].m_bytesPerImage;
            RHI::BufferInitRequest req;
            req.m_buffer = readbackBufs[f].get();
            req.m_descriptor = desc;
            if (m_readbackPool->InitBuffer(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] irradiance readback buffer init failed.");
                return result;
            }
        }

        // ---- Record: src cube -> shader read, irradiance -> shader write, dispatch, read back ----
        RHI::CommandList* cmd = m_recorder->GetCommandList();

        RHI::ImageBarrier srcReadBarrier = RHI::ConvertToImageShaderRead(srcCube);
        cmd->QueueBarrier(srcReadBarrier);
        RHI::ImageBarrier irrWriteBarrier = RHI::ConvertToImageShaderWrite(*irrImg);
        cmd->QueueBarrier(irrWriteBarrier);
        cmd->FlushBarriers();

        cmd->SetPipelineState(*m_irradiancePSO);
        cmd->BindShaderInputsForDispatch(*bindings);
        {
            RHI::DispatchItem di;
            di.m_arguments = RHI::DispatchArguments(
                RHI::DispatchDirect(kIrradianceSize, kIrradianceSize, kNumCubeFaces, 8, 8, 1));
            di.m_pipelineState = m_irradiancePSO.get();
            cmd->Submit(di);
        }

        RHI::ImageBarrier irrReadBarrier = RHI::ConvertToImageCopyRead(*irrImg);
        cmd->QueueBarrier(irrReadBarrier);
        cmd->FlushBarriers();

        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            RHI::CopyImageToBufferDescriptor rb;
            rb.m_sourceImage              = irrImg.get();
            rb.m_sourceSubresource        = RHI::ImageSubresource(0, static_cast<uint16_t>(f));
            rb.m_sourceOrigin             = RHI::Origin(0, 0, 0);
            rb.m_sourceSize               = subresLayouts[f].m_size;
            rb.m_destinationBuffer        = readbackBufs[f].get();
            rb.m_destinationOffset        = 0;
            rb.m_destinationBytesPerRow   = subresLayouts[f].m_bytesPerRow;
            rb.m_destinationBytesPerImage = subresLayouts[f].m_bytesPerImage;
            rb.m_destinationFormat        = kCubeFormat;
            cmd->Submit(RHI::CopyItem(rb));
        }

        cmd->Close();

        RHI::CommandList* lists[] = { cmd };
        m_queue->ExecuteCommands(lists);
        m_queue->FlushCommands(*m_fence);
        m_recorder->Reset();

        // ---- De-pad rows into a tight, face-major buffer (single mip) ----
        result.faceSize  = kIrradianceSize;
        result.mipLevels = 1;
        result.format    = kCubeFormat;
        const uint32_t tightRow = kIrradianceSize * kCubeBytesPP;
        result.faceBytes.resize(static_cast<size_t>(tightRow) * kIrradianceSize * kNumCubeFaces);

        size_t dstOffset = 0;
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            const auto& layout = subresLayouts[f];

            RHI::BufferMapRequest mapReq;
            mapReq.m_buffer     = readbackBufs[f].get();
            mapReq.m_byteOffset = 0;
            mapReq.m_byteCount  = layout.m_bytesPerImage;
            RHI::BufferMapResponse mapResp;
            if (m_readbackPool->MapBuffer(mapReq, mapResp) != RHI::ResultCode::Success || !mapResp.m_data)
            {
                LOG_ERROR("[EnvironmentBaker] irradiance readback map failed.");
                return BakedCubemap{};
            }

            const auto* srcBytes = static_cast<const uint8_t*>(mapResp.m_data);
            for (uint32_t row = 0; row < layout.m_rowCount; ++row)
            {
                memcpy(result.faceBytes.data() + dstOffset + row * tightRow,
                    srcBytes + static_cast<size_t>(row) * layout.m_bytesPerRow,
                    tightRow);
            }
            m_readbackPool->UnmapBuffer(*readbackBufs[f]);

            dstOffset += static_cast<size_t>(tightRow) * kIrradianceSize;
        }

        return result;
    }

    BakedCubemap EnvironmentBaker::BakePrefilter(RHI::Image& srcCube, uint32_t srcMipLevels)
    {
        BakedCubemap result;

        // ---- Prefiltered cube (UAV target, one mip per roughness level) ----
        Ptr<RHI::Image> prefImg = m_factory->CreateImage();
        {
            RHI::ImageInitRequest req;
            req.m_image = prefImg.get();
            req.m_descriptor = RHI::ImageDescriptor::CreateCubemap(
                RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead,
                kPrefilterSize, kCubeFormat);
            req.m_descriptor.m_mipLevels = kPrefilterMips;
            if (m_imagePool->InitImage(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] prefilter image init failed.");
                return result;
            }
        }

        // Sky cube as a TextureCube SRV; the shader picks a mip per sample by solid angle.
        Ptr<RHI::ImageView> srcView = m_factory->CreateImageView();
        {
            RHI::ImageViewDescriptor viewDesc = RHI::ImageViewDescriptor::CreateCubemap(
                kCubeFormat, 0, static_cast<uint16_t>(srcMipLevels - 1));
            if (srcView->Init(srcCube, viewDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] prefilter source view init failed.");
                return result;
            }
        }

        // One UAV view + one ShaderBindings per mip: each mip is a distinct roughness, and a
        // ShaderBindings instance holds a single data set (see 3a), so they cannot be shared.
        eastl::vector<Ptr<RHI::ImageView>>      mipViews(kPrefilterMips);
        eastl::vector<Ptr<RHI::ShaderBindings>> mipBindings(kPrefilterMips);
        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            mipViews[mip] = m_factory->CreateImageView();
            {
                RHI::ImageViewDescriptor viewDesc;
                viewDesc.m_mipSliceMin    = static_cast<uint16_t>(mip);
                viewDesc.m_mipSliceMax    = static_cast<uint16_t>(mip);
                viewDesc.m_arraySliceMin  = 0;
                viewDesc.m_arraySliceMax  = static_cast<uint16_t>(kNumCubeFaces - 1);
                viewDesc.m_overrideFormat = kCubeFormat;
                if (mipViews[mip]->Init(*prefImg, viewDesc) != RHI::ResultCode::Success)
                {
                    LOG_ERROR("[EnvironmentBaker] prefilter mip view init failed.");
                    return result;
                }
            }

            mipBindings[mip] = m_factory->CreateShaderBindings();
            RHI::ShaderBindings::Descriptor bindingsDesc;
            bindingsDesc.m_layout  = m_prefilterLayout;
            bindingsDesc.m_spaceId = 0;
            if (mipBindings[mip]->Init(*m_device, bindingsDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] prefilter ShaderBindings init failed.");
                return result;
            }

            // roughness N/(mipLevels-1): mip 0 -> 0 (mirror), last -> 1.
            const float    roughness   = kPrefilterMips > 1
                ? static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1)
                : 0.0f;
            const uint32_t sampleCount = kPrefilterSamples;

            if (auto* in = mipBindings[mip]->FindImageInput(RHI::InputName("g_SrcCube")))
            {
                in->SetView(0, srcView.get());
            }
            if (auto* in = mipBindings[mip]->FindSamplerInput(RHI::InputName("g_Sampler")))
            {
                in->SetState(0, RHI::SamplerState::Create(
                    RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));
            }
            if (auto* in = mipBindings[mip]->FindImageInput(RHI::InputName("g_Prefiltered")))
            {
                in->SetView(0, mipViews[mip].get());
            }
            // cbuffer members reflect as individual constants by variable name.
            if (auto* in = mipBindings[mip]->FindConstantInput(RHI::InputName("g_Roughness")))
            {
                in->SetData(&roughness, sizeof(roughness));
            }
            if (auto* in = mipBindings[mip]->FindConstantInput(RHI::InputName("g_SampleCount")))
            {
                in->SetData(&sampleCount, sizeof(sampleCount));
            }

            RHI::ShaderInputCompiler& compiler = m_factory->AcquireShaderInputCompiler(*m_device);
            compiler.Compile(*mipBindings[mip]);
        }

        // Readback buffers, one per (face, mip). GetSubresourceLayouts indexes globally
        // (mip + array*mipLevels), so span the full grid and look up via GetImageSubresourceIndex.
        eastl::vector<RHI::ImageSubresourceLayout> subresLayouts(
            static_cast<size_t>(kPrefilterMips) * kNumCubeFaces);
        prefImg->GetSubresourceLayouts(
            RHI::ImageSubresourceRange(0, static_cast<uint16_t>(kPrefilterMips - 1),
                                       0, static_cast<uint16_t>(kNumCubeFaces - 1)),
            subresLayouts.data(), nullptr);

        auto FaceMipLayout = [&](uint32_t f, uint32_t mip) -> const RHI::ImageSubresourceLayout&
        {
            return subresLayouts[RHI::GetImageSubresourceIndex(
                static_cast<uint16_t>(mip), static_cast<uint16_t>(f),
                static_cast<uint16_t>(kPrefilterMips))];
        };
        // Linear index into readbackBufs / the face-major, mip-inner byte order.
        auto SubresIndex = [&](uint32_t f, uint32_t mip) -> size_t
        {
            return static_cast<size_t>(f) * kPrefilterMips + mip;
        };

        eastl::vector<Ptr<RHI::Buffer>> readbackBufs(
            static_cast<size_t>(kNumCubeFaces) * kPrefilterMips);
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
            {
                const size_t idx = SubresIndex(f, mip);
                readbackBufs[idx] = m_factory->CreateBuffer();
                RHI::BufferDescriptor desc;
                desc.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
                desc.m_byteCount = FaceMipLayout(f, mip).m_bytesPerImage;
                RHI::BufferInitRequest req;
                req.m_buffer = readbackBufs[idx].get();
                req.m_descriptor = desc;
                if (m_readbackPool->InitBuffer(req) != RHI::ResultCode::Success)
                {
                    LOG_ERROR("[EnvironmentBaker] prefilter readback buffer init failed.");
                    return result;
                }
            }
        }

        // One dispatch per mip. Mips write disjoint subresources and only read the (read-only)
        // src, so no inter-mip barrier is needed — unlike the sky chain where mip N reads N-1.
        RHI::CommandList* cmd = m_recorder->GetCommandList();

        RHI::ImageBarrier srcReadBarrier = RHI::ConvertToImageShaderRead(srcCube);
        cmd->QueueBarrier(srcReadBarrier);
        RHI::ImageBarrier prefWriteBarrier = RHI::ConvertToImageShaderWrite(*prefImg);
        cmd->QueueBarrier(prefWriteBarrier);
        cmd->FlushBarriers();

        cmd->SetPipelineState(*m_prefilterPSO);
        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            cmd->BindShaderInputsForDispatch(*mipBindings[mip]);

            const uint32_t dstSize = eastl::max(1u, kPrefilterSize >> mip);
            RHI::DispatchItem di;
            di.m_arguments = RHI::DispatchArguments(
                RHI::DispatchDirect(dstSize, dstSize, kNumCubeFaces, 8, 8, 1));
            di.m_pipelineState = m_prefilterPSO.get();
            cmd->Submit(di);
        }

        RHI::ImageBarrier prefReadBarrier = RHI::ConvertToImageCopyRead(*prefImg);
        cmd->QueueBarrier(prefReadBarrier);
        cmd->FlushBarriers();

        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
            {
                RHI::CopyImageToBufferDescriptor rb;
                rb.m_sourceImage              = prefImg.get();
                rb.m_sourceSubresource        = RHI::ImageSubresource(
                    static_cast<uint16_t>(mip), static_cast<uint16_t>(f));
                rb.m_sourceOrigin             = RHI::Origin(0, 0, 0);
                rb.m_sourceSize               = FaceMipLayout(f, mip).m_size;
                rb.m_destinationBuffer        = readbackBufs[SubresIndex(f, mip)].get();
                rb.m_destinationOffset        = 0;
                rb.m_destinationBytesPerRow   = FaceMipLayout(f, mip).m_bytesPerRow;
                rb.m_destinationBytesPerImage = FaceMipLayout(f, mip).m_bytesPerImage;
                rb.m_destinationFormat        = kCubeFormat;
                cmd->Submit(RHI::CopyItem(rb));
            }
        }

        cmd->Close();

        RHI::CommandList* lists[] = { cmd };
        m_queue->ExecuteCommands(lists);
        m_queue->FlushCommands(*m_fence);
        m_recorder->Reset();

        // ---- De-pad rows into a tight, FACE-MAJOR, MIP-INNER buffer (same contract as
        //      BakeSky: [f0m0][f0m1]...[f0mN][f1m0]...). ----
        result.faceSize  = kPrefilterSize;
        result.mipLevels = kPrefilterMips;
        result.format    = kCubeFormat;
        size_t totalBytes = 0;
        for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
        {
            const uint32_t s = eastl::max(1u, kPrefilterSize >> mip);
            totalBytes += static_cast<size_t>(s) * s * kCubeBytesPP;
        }
        totalBytes *= kNumCubeFaces;
        result.faceBytes.resize(totalBytes);

        size_t dstOffset = 0;
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
            {
                const auto&    layout   = FaceMipLayout(f, mip);
                const size_t   idx      = SubresIndex(f, mip);
                const uint32_t mipSize  = eastl::max(1u, kPrefilterSize >> mip);
                const uint32_t tightRow = mipSize * kCubeBytesPP;

                RHI::BufferMapRequest mapReq;
                mapReq.m_buffer     = readbackBufs[idx].get();
                mapReq.m_byteOffset = 0;
                mapReq.m_byteCount  = layout.m_bytesPerImage;
                RHI::BufferMapResponse mapResp;
                if (m_readbackPool->MapBuffer(mapReq, mapResp) != RHI::ResultCode::Success || !mapResp.m_data)
                {
                    LOG_ERROR("[EnvironmentBaker] prefilter readback map failed.");
                    return BakedCubemap{};
                }

                const auto* srcBytes = static_cast<const uint8_t*>(mapResp.m_data);
                for (uint32_t row = 0; row < layout.m_rowCount; ++row)
                {
                    memcpy(result.faceBytes.data() + dstOffset + row * tightRow,
                        srcBytes + static_cast<size_t>(row) * layout.m_bytesPerRow,
                        tightRow);
                }
                m_readbackPool->UnmapBuffer(*readbackBufs[idx]);

                dstOffset += static_cast<size_t>(tightRow) * mipSize;
            }
        }

        return result;
    }
}
