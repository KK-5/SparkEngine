// BRDFLutGen — offline generator for the split-sum BRDF LUT (DFG table).
//
// The table is a mathematical constant: scene-independent, resolution-independent, and it
// never changes. So it is baked HERE, once, by hand, and the .ktx2 is checked into
// Engine/Asset/Image/ -- the runtime only loads it and never carries this bake.
//
// Headless: init RHI + AssetManager, one compute dispatch over a kSize^2 RG16F image, read
// it back, write BRDFLut.ktx2 next to the engine assets plus a PNG for eyeballing (a
// correct table is bright at the lower left and dark toward the upper right; a flipped uv
// convention is obvious there and nowhere else until it reaches a metal surface).

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
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
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>
#include <RHI/Resource/ShaderInput/ShaderInputCompiler.h>
#include <RHI/Backend/DX12/RHISystem.h>

#include <Resource/AssetManager.h>
#include <VFS/VFSSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <ktx.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <EASTL/vector.h>
#include <cstdint>
#include <cstring>
#include <cmath>

using namespace Spark;

namespace
{
    constexpr uint32_t    kSize        = 128;
    constexpr uint32_t    kSampleCount = 1024;
    constexpr RHI::Format kFormat      = RHI::Format::R16G16_FLOAT;
    constexpr uint32_t    kBytesPP     = 4;   // RG16F

    constexpr uint32_t kVkFormatR16G16_SFLOAT = 83;

    const char* kOutKtx2 = ENGINE_ASSET_DIR "/Image/BRDFLut.ktx2";
    const char* kOutPng  = "brdf_lut.png";

    float HalfToFloat(uint16_t h)
    {
        const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
        uint32_t       exp  = (h >> 10) & 0x1Fu;
        uint32_t       mant = h & 0x3FFu;
        uint32_t       bits;

        if (exp == 0)
        {
            if (mant == 0)
            {
                bits = sign;
            }
            else
            {
                exp = 1;
                while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
                mant &= 0x3FFu;
                bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
            }
        }
        else if (exp == 0x1Fu)
        {
            bits = sign | 0x7F800000u | (mant << 13);
        }
        else
        {
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }

        float out;
        memcpy(&out, &bits, sizeof(out));
        return out;
    }

    //! Tool-local KTX2 writer: 2D, single level, single layer, single face. The asset system
    //! will grow its own serializer when disk caching lands; this exists only to get the
    //! table into a container the loader can read.
    bool WriteKtx2(const char* path, const eastl::vector<uint8_t>& bytes)
    {
        ktxTextureCreateInfo info{};
        info.vkFormat        = kVkFormatR16G16_SFLOAT;
        info.baseWidth       = kSize;
        info.baseHeight      = kSize;
        info.baseDepth       = 1;
        info.numDimensions   = 2;
        info.numLevels       = 1;
        info.numLayers       = 1;
        info.numFaces        = 1;
        info.generateMipmaps = KTX_FALSE;

        ktxTexture2*  tex = nullptr;
        KTX_error_code res = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex);
        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[BRDFLutGen] ktxTexture2_Create failed: {}", static_cast<int>(res));
            return false;
        }

        res = ktxTexture_SetImageFromMemory(ktxTexture(tex), 0, 0, 0,
                                            bytes.data(), static_cast<ktx_size_t>(bytes.size()));
        if (res == KTX_SUCCESS)
        {
            res = ktxTexture_WriteToNamedFile(ktxTexture(tex), path);
        }
        ktxTexture_Destroy(ktxTexture(tex));

        if (res != KTX_SUCCESS)
        {
            LOG_ERROR("[BRDFLutGen] failed to write {}: {}", path, static_cast<int>(res));
            return false;
        }
        LOG_INFO("[BRDFLutGen] wrote {} ({}x{} RG16F, {}B payload)", path, kSize, kSize, bytes.size());
        return true;
    }

    //! Read the file back through AssetManager and compare byte for byte. Closes the loop
    //! in one run: the KTX2 written here and the load path the renderer will use are only
    //! ever correct relative to each other.
    bool VerifyRoundTrip(const eastl::vector<uint8_t>& expected)
    {
        auto* am = Service<Resource::AssetManager>::Get();
        Ptr<Resource::Asset> asset = am->LoadAsset(
            am->MakeAssetId("engine://Image/BRDFLut.ktx2"));
        if (!asset || !asset->IsReady())
        {
            LOG_ERROR("[BRDFLutGen] round trip: the LUT asset is not Ready.");
            return false;
        }

        const auto* data = static_cast<const Resource::ImageAsset*>(asset.get())->GetImageData();
        if (!data || data->GetWidth() != kSize || data->GetHeight() != kSize
            || data->GetMipLevels() != 1 || data->GetArrayLayers() != 1
            || data->GetFormat() != kFormat)
        {
            LOG_ERROR("[BRDFLutGen] round trip: shape/format mismatch.");
            return false;
        }
        if (data->GetTextureBytes().size() != expected.size()
            || memcmp(data->GetTextureBytes().data(), expected.data(), expected.size()) != 0)
        {
            LOG_ERROR("[BRDFLutGen] round trip: payload differs from what was written.");
            return false;
        }

        LOG_INFO("[BRDFLutGen] round trip OK: {}x{}, 1 mip, {}B identical.",
                 data->GetWidth(), data->GetHeight(), data->GetTextureBytes().size());
        return true;
    }

    void WritePreviewPng(const char* path, const eastl::vector<uint8_t>& bytes)
    {
        eastl::vector<uint8_t> rgb(static_cast<size_t>(kSize) * kSize * 3);
        const auto* half = reinterpret_cast<const uint16_t*>(bytes.data());
        for (size_t i = 0; i < static_cast<size_t>(kSize) * kSize; ++i)
        {
            const float scale = HalfToFloat(half[i * 2 + 0]);
            const float bias  = HalfToFloat(half[i * 2 + 1]);
            rgb[i * 3 + 0] = static_cast<uint8_t>(eastl::min(1.0f, eastl::max(0.0f, scale)) * 255.0f + 0.5f);
            rgb[i * 3 + 1] = static_cast<uint8_t>(eastl::min(1.0f, eastl::max(0.0f, bias))  * 255.0f + 0.5f);
            rgb[i * 3 + 2] = 0;
        }

        if (stbi_write_png(path, kSize, kSize, 3, rgb.data(), kSize * 3))
        {
            LOG_INFO("[BRDFLutGen] wrote {} (red = scale, green = bias)", path);
        }
        else
        {
            LOG_ERROR("[BRDFLutGen] failed to write {}", path);
        }
    }
}

int main(int, char**)
{
    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem> logger = eastl::make_unique<SpdLogSystem>(logConfig);

    // --- RHI device (headless) ---
    auto rhiSystem = CreateSystem<RHI::DX12::RHISystem>();
    rhiSystem->Init();
    auto* rhi = Service<RHI::RHIInterface>::Get();
    RHI::PhysicalDeviceList devices = rhi->EnumeratePhysicalDevices();
    if (devices.empty())
    {
        LOG_ERROR("[BRDFLutGen] No physical devices.");
        return 1;
    }
    RHI::DeviceDescriptor devDesc;
    devDesc.m_frameCountMax = 2;
    if (rhi->InitDevice(*devices[0], devDesc) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[BRDFLutGen] InitDevice failed.");
        return 1;
    }
    auto* factory = rhi->GetRHIFactory();
    auto* device  = rhi->GetDevice();

    // --- AssetManager: only needed to resolve + compile the bake shader ---
    auto fileSystem = CreateSystem<VFSSystem>();
    fileSystem->Init();
    fileSystem->Mount("engine", ENGINE_ASSET_DIR);

    auto assetManager = CreateSystem<Resource::SparkAssetManager>();
    assetManager->Init();

    auto* am = Service<Resource::AssetManager>::Get();
    Ptr<Resource::ShaderAsset> shader = am->LoadAsset<Resource::ShaderAsset>(
        Resource::AssetId::Of<Resource::ShaderAsset>("engine://Shaders/Image/BRDFLutBake.hlsl"));
    if (!shader || shader->GetStatus() != Resource::AssetStatus::Ready)
    {
        LOG_ERROR("[BRDFLutGen] Failed to load BRDFLutBake.hlsl.");
        return 1;
    }
    auto* shaderData = shader->GetShaderData();
    const auto* csBytecode = shaderData
        ? shaderData->GetStageBytecode(RHI::ShaderStage::Compute)
        : nullptr;
    if (!csBytecode)
    {
        LOG_ERROR("[BRDFLutGen] BRDFLutBake.hlsl has no compute bytecode.");
        return 1;
    }

    // --- Pipeline (layout reflected from the shader, same path as EnvironmentBaker) ---
    Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shader);
    if (built.stageMask == RHI::ShaderStageMask::None)
    {
        LOG_ERROR("[BRDFLutGen] Bake shader produced no shader inputs.");
        return 1;
    }
    Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
    layout->AddShaderInputDescriptors(built.list, built.stageMask);
    layout->Finalize();

    Ptr<RHI::ShaderStageFunction> csFunc =
        factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
    csFunc->SetByteCode(csBytecode->bytecode);
    csFunc->Finalize();

    Ptr<RHI::PipelineLibrary> pipelineLibrary = factory->CreatePipelineLibrary();
    pipelineLibrary->Init(*device, RHI::PipelineLibraryDescriptor{});

    Ptr<RHI::PipelineState> pso = factory->CreatePipelineState();
    {
        RHI::PipelineStateDescriptorForDispatch psoDesc;
        psoDesc.m_computeFunction          = csFunc;
        psoDesc.m_pipelineLayoutDescriptor = layout;
        if (pso->Init(*device, psoDesc, pipelineLibrary.get()) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] Compute PSO init failed.");
            return 1;
        }
    }

    // --- Queue / recorder / fence / pools ---
    Ptr<RHI::CommandQueue> queue = factory->CreateCommandQueue();
    {
        RHI::CommandQueueDescriptor desc;
        desc.m_hardwareQueueClass = RHI::HardwareQueueClass::Graphics;
        desc.m_maxFrameQueueDepth = 1;
        if (queue->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] Command queue init failed.");
            return 1;
        }
    }

    Ptr<RHI::CommandRecorder> recorder = factory->CreateCommandRecorder();
    {
        RHI::CommandRecorderDescriptor desc;
        desc.m_queue = RHI::HardwareQueueClass::Graphics;
        if (recorder->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] Command recorder init failed.");
            return 1;
        }
    }

    Ptr<RHI::Fence> fence = factory->CreateFence();
    if (fence->Init(*device, RHI::FenceState::Reset) != RHI::ResultCode::Success)
    {
        LOG_ERROR("[BRDFLutGen] Fence init failed.");
        return 1;
    }

    Ptr<RHI::ImagePool> imagePool = factory->CreateImagePool();
    {
        RHI::ImagePoolDescriptor desc;
        desc.m_bindFlags = RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead;
        if (imagePool->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] Image pool init failed.");
            return 1;
        }
    }

    Ptr<RHI::BufferPool> readbackPool = factory->CreateBufferPool();
    {
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
        desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Read;
        desc.m_bindFlags        = RHI::BufferBindFlags::CopyWrite;
        desc.m_sharedQueueMask  = RHI::HardwareQueueClassMask::All;
        if (readbackPool->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] Readback buffer pool init failed.");
            return 1;
        }
    }

    // --- Target image + UAV ---
    Ptr<RHI::Image> lutImg = factory->CreateImage();
    {
        RHI::ImageInitRequest req;
        req.m_image = lutImg.get();
        req.m_descriptor = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead,
            kSize, kSize, kFormat);
        req.m_descriptor.m_mipLevels = 1;
        if (imagePool->InitImage(req) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] LUT image init failed.");
            return 1;
        }
    }

    Ptr<RHI::ImageView> lutView = factory->CreateImageView();
    {
        RHI::ImageViewDescriptor viewDesc;
        viewDesc.m_mipSliceMin    = 0;
        viewDesc.m_mipSliceMax    = 0;
        viewDesc.m_arraySliceMin  = 0;
        viewDesc.m_arraySliceMax  = 0;
        viewDesc.m_overrideFormat = kFormat;
        if (lutView->Init(*lutImg, viewDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] LUT view init failed.");
            return 1;
        }
    }

    Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
    {
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 0;
        if (bindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] ShaderBindings init failed.");
            return 1;
        }
    }
    if (auto* in = bindings->FindImageInput(RHI::InputName("g_Lut")))
    {
        in->SetView(0, lutView.get());
    }
    if (auto* in = bindings->FindConstantInput(RHI::InputName("g_SampleCount")))
    {
        in->SetData(&kSampleCount, sizeof(kSampleCount));
    }
    {
        RHI::ShaderInputCompiler& compiler = factory->AcquireShaderInputCompiler(*device);
        compiler.Compile(*bindings);
    }

    // --- Readback buffer sized by the GPU's (row-aligned) layout ---
    RHI::ImageSubresourceLayout subresLayout;
    lutImg->GetSubresourceLayouts(RHI::ImageSubresourceRange(0, 0, 0, 0), &subresLayout, nullptr);

    Ptr<RHI::Buffer> readbackBuf = factory->CreateBuffer();
    {
        RHI::BufferDescriptor desc;
        desc.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
        desc.m_byteCount = subresLayout.m_bytesPerImage;
        RHI::BufferInitRequest req;
        req.m_buffer     = readbackBuf.get();
        req.m_descriptor = desc;
        if (readbackPool->InitBuffer(req) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[BRDFLutGen] Readback buffer init failed.");
            return 1;
        }
    }

    // --- Dispatch + readback ---
    RHI::CommandList* cmd = recorder->GetCommandList();

    RHI::ImageBarrier writeBarrier = RHI::ConvertToImageShaderWrite(*lutImg);
    cmd->QueueBarrier(writeBarrier);
    cmd->FlushBarriers();

    cmd->SetPipelineState(*pso);
    cmd->BindShaderInputsForDispatch(*bindings);
    {
        RHI::DispatchItem di;
        di.m_arguments = RHI::DispatchArguments(RHI::DispatchDirect(kSize, kSize, 1, 8, 8, 1));
        cmd->Submit(di);
    }

    RHI::ImageBarrier readBarrier = RHI::ConvertToImageCopyRead(*lutImg);
    cmd->QueueBarrier(readBarrier);
    cmd->FlushBarriers();

    {
        RHI::CopyImageToBufferDescriptor rb;
        rb.m_sourceImage              = lutImg.get();
        rb.m_sourceSubresource        = RHI::ImageSubresource(0, 0);
        rb.m_sourceOrigin             = RHI::Origin(0, 0, 0);
        rb.m_sourceSize               = subresLayout.m_size;
        rb.m_destinationBuffer        = readbackBuf.get();
        rb.m_destinationOffset        = 0;
        rb.m_destinationBytesPerRow   = subresLayout.m_bytesPerRow;
        rb.m_destinationBytesPerImage = subresLayout.m_bytesPerImage;
        rb.m_destinationFormat        = kFormat;
        cmd->Submit(RHI::CopyItem(rb));
    }

    cmd->Close();

    RHI::CommandList* lists[] = { cmd };
    queue->ExecuteCommands(lists);
    queue->FlushCommands(*fence);
    recorder->Reset();

    // --- De-pad rows into a tight buffer ---
    eastl::vector<uint8_t> tight(static_cast<size_t>(kSize) * kSize * kBytesPP);
    {
        RHI::BufferMapRequest mapReq;
        mapReq.m_buffer     = readbackBuf.get();
        mapReq.m_byteOffset = 0;
        mapReq.m_byteCount  = subresLayout.m_bytesPerImage;
        RHI::BufferMapResponse mapResp;
        if (readbackPool->MapBuffer(mapReq, mapResp) != RHI::ResultCode::Success || !mapResp.m_data)
        {
            LOG_ERROR("[BRDFLutGen] Readback map failed.");
            return 1;
        }

        const auto*    srcBytes = static_cast<const uint8_t*>(mapResp.m_data);
        const uint32_t tightRow = kSize * kBytesPP;
        for (uint32_t row = 0; row < subresLayout.m_rowCount; ++row)
        {
            memcpy(tight.data() + static_cast<size_t>(row) * tightRow,
                   srcBytes + static_cast<size_t>(row) * subresLayout.m_bytesPerRow,
                   tightRow);
        }
        readbackPool->UnmapBuffer(*readbackBuf);
    }

    // Cheap sanity check on the axes: the table's scale term is near 1 for a smooth surface
    // seen head-on and drops off toward grazing / rough. Getting these two backwards is the
    // one failure mode a flipped uv convention produces, and it is silent downstream.
    {
        const auto* half = reinterpret_cast<const uint16_t*>(tight.data());
        auto Scale = [&](uint32_t x, uint32_t y)
        {
            return HalfToFloat(half[(static_cast<size_t>(y) * kSize + x) * 2]);
        };
        LOG_INFO("[BRDFLutGen] scale: smooth/head-on={:.3f}, rough/head-on={:.3f}, "
                 "smooth/grazing={:.3f}",
                 Scale(kSize - 1, 0), Scale(kSize - 1, kSize - 1), Scale(0, 0));
    }

    if (!WriteKtx2(kOutKtx2, tight))
    {
        return 1;
    }
    WritePreviewPng(kOutPng, tight);

    if (!VerifyRoundTrip(tight))
    {
        return 1;
    }

    LOG_INFO("[BRDFLutGen] Done.");
    return 0;
}
