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

    BakedCubemap EnvironmentBaker::Bake(const ImageAssetRawData& equirect, uint32_t faceSize)
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
        {
            RHI::ImageInitRequest req;
            req.m_image = cubeImg.get();
            req.m_descriptor = RHI::ImageDescriptor::CreateCubemap(
                RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead,
                faceSize, kCubeFormat);
            req.m_descriptor.m_mipLevels = 1;
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

        // ---- Readback buffers: one per face (offset 0, always aligned) ----
        // GetSubresourceLayouts writes by GLOBAL subresource index (mip + array*mipLevels),
        // so the output must span the full mip*face grid, and a face's base mip is fetched
        // via GetImageSubresourceIndex rather than assuming index == face. Today mipLevels
        // is 1 (index == face), but this stays correct once a mip chain is added (M4).
        const uint16_t cubeMipLevels = cubeImg->GetDescriptor().m_mipLevels;
        eastl::vector<RHI::ImageSubresourceLayout> subresLayouts(
            static_cast<size_t>(cubeMipLevels) * kNumCubeFaces);
        cubeImg->GetSubresourceLayouts(
            RHI::ImageSubresourceRange(0, 0, 0, static_cast<uint16_t>(kNumCubeFaces - 1)),
            subresLayouts.data(), nullptr);

        // Base-mip (mip 0) layout of cube face f.
        auto FaceLayout = [&](uint32_t f) -> const RHI::ImageSubresourceLayout&
        {
            return subresLayouts[RHI::GetImageSubresourceIndex(0, static_cast<uint16_t>(f), cubeMipLevels)];
        };

        eastl::array<Ptr<RHI::Buffer>, kNumCubeFaces> readbackBufs{};
        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            readbackBufs[f] = m_factory->CreateBuffer();
            RHI::BufferDescriptor desc;
            desc.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
            desc.m_byteCount = FaceLayout(f).m_bytesPerImage;
            RHI::BufferInitRequest req;
            req.m_buffer = readbackBufs[f].get();
            req.m_descriptor = desc;
            if (m_readbackPool->InitBuffer(req) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[EnvironmentBaker] readback buffer init failed.");
                return result;
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

        // cube -> copy read, then read each face back.
        RHI::ImageBarrier cubeReadBarrier = RHI::ConvertToImageCopyRead(*cubeImg);
        cmd->QueueBarrier(cubeReadBarrier);
        cmd->FlushBarriers();

        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            RHI::CopyImageToBufferDescriptor rb;
            rb.m_sourceImage            = cubeImg.get();
            rb.m_sourceSubresource      = RHI::ImageSubresource(0, static_cast<uint16_t>(f));
            rb.m_sourceOrigin           = RHI::Origin(0, 0, 0);
            rb.m_sourceSize             = FaceLayout(f).m_size;
            rb.m_destinationBuffer      = readbackBufs[f].get();
            rb.m_destinationOffset      = 0;
            rb.m_destinationBytesPerRow = FaceLayout(f).m_bytesPerRow;
            rb.m_destinationBytesPerImage = FaceLayout(f).m_bytesPerImage;
            rb.m_destinationFormat      = kCubeFormat;
            cmd->Submit(RHI::CopyItem(rb));
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

        // ---- Read back faces, de-pad rows into a tight, face-major buffer ----
        const uint32_t tightRowBytes = faceSize * kCubeBytesPP;
        result.faceSize = faceSize;
        result.format   = kCubeFormat;
        result.faceBytes.resize(static_cast<size_t>(tightRowBytes) * faceSize * kNumCubeFaces);

        for (uint32_t f = 0; f < kNumCubeFaces; ++f)
        {
            RHI::BufferMapRequest mapReq;
            mapReq.m_buffer     = readbackBufs[f].get();
            mapReq.m_byteOffset = 0;
            mapReq.m_byteCount  = FaceLayout(f).m_bytesPerImage;
            RHI::BufferMapResponse mapResp;
            if (m_readbackPool->MapBuffer(mapReq, mapResp) != RHI::ResultCode::Success || !mapResp.m_data)
            {
                LOG_ERROR("[EnvironmentBaker] readback map failed.");
                return BakedCubemap{};
            }

            const auto* srcBytes = static_cast<const uint8_t*>(mapResp.m_data);
            const uint32_t alignedRowBytes = FaceLayout(f).m_bytesPerRow;
            uint8_t* dstFace = result.faceBytes.data() + static_cast<size_t>(f) * tightRowBytes * faceSize;
            for (uint32_t row = 0; row < faceSize; ++row)
            {
                memcpy(dstFace + static_cast<size_t>(row) * tightRowBytes,
                       srcBytes + static_cast<size_t>(row) * alignedRowBytes,
                       tightRowBytes);
            }
            m_readbackPool->UnmapBuffer(*readbackBufs[f]);
        }

        return result;
    }
}
