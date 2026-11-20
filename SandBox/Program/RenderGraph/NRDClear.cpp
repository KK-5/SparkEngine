#include <Base.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Command/CommandList.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandRecorder.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Context/UniqueRHIHandle.h>
#include <RHI/Device/Device.h>
#include <RHI/Factory.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Pipeline/ShaderStageFunction.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/ResourceBuilder.h>

#include <Pass/ComputePass.h>

#include "../Common/RenderGraphUtil.h"

#include <EASTL/array.h>
#include <EASTL/optional.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <NRD.h>

#include <string.h>

using namespace Spark;

namespace
{
    constexpr uint16_t kTextureSize = 32;
    constexpr uint16_t kClearWidth = 19;
    constexpr uint16_t kClearHeight = 11;
    constexpr uint16_t kHalfOne = 0x3c00;
    constexpr RHI::Format kFormat = RHI::Format::R16G16B16A16_FLOAT;
    constexpr auto kImageFlags = RHI::ImageBindFlags::ShaderWrite
        | RHI::ImageBindFlags::CopyRead | RHI::ImageBindFlags::CopyWrite;
    constexpr auto kQueue = RHI::HardwareQueueClass::Graphics;

    struct ClearField
    {
        RHI::InputName m_name;
        uint32_t m_offset;
    };

    // Packed int2 fields at the start of the NRD 4.18 Clear constant buffer.
    constexpr uint32_t kInt2Bytes = 8;
    constexpr uint32_t kClearConstantBytes = 2 * kInt2Bytes;
    const ClearField kClearFields[] = {
        {RHI::InputName("gRectSize"), 0},
        {RHI::InputName("gDispatchOutputRectOrigin"), 8},
    };
    const RHI::InputName kOutputInput{"gOut"};
    const RHI::AttachmentId kOutput{"NRDClearOutput"};

    const nrd::DispatchDesc* FindOutputClear(
        const nrd::InstanceDesc& instance, const nrd::DispatchDesc* dispatches, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& dispatch = dispatches[i];
            if (dispatch.resourcesNum == 1
                && dispatch.resources[0].type == nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST
                && dispatch.resources[0].descriptorType == nrd::DescriptorType::STORAGE_TEXTURE
                && dispatch.pipelineIndex < instance.pipelinesNum
                && eastl::string_view(instance.pipelines[dispatch.pipelineIndex].shaderIdentifier)
                    == "Clear.cs.hlsl|FLOAT=1")
            {
                return &dispatch;
            }
        }
        return nullptr;
    }

    bool RegisterClearPass(Render::PassContext& passes, RHI::Factory& factory, RHI::Device& device,
        RHI::RHIHandle output)
    {
        const auto& library = *nrd::GetLibraryDesc();
        if (library.versionMajor != 4 || library.versionMinor != 18)
        {
            LOG_ERROR("[NRDClear] Expected NRD 4.18, got {}.{}.",
                static_cast<unsigned>(library.versionMajor), static_cast<unsigned>(library.versionMinor));
            return false;
        }

        const nrd::DenoiserDesc denoiser{1, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR};
        nrd::InstanceCreationDesc creation{};
        creation.denoisers = &denoiser;
        creation.denoisersNum = 1;
        nrd::Instance* instance = nullptr;
        auto result = nrd::CreateInstance(creation, instance);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDClear] CreateInstance failed ({}).", static_cast<unsigned>(result));
            return false;
        }
        auto destroyInstance = [](nrd::Instance* value) { nrd::DestroyInstance(*value); };
        UniquePtr<nrd::Instance, decltype(destroyInstance)> owner(instance, destroyInstance);

        // Column-major perspective projection and a stationary camera.
        constexpr float projection[] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.001f, 1.0f,
            0.0f, 0.0f, -0.1001f, 0.0f
        };
        nrd::CommonSettings settings{};
        memcpy(settings.viewToClipMatrix, projection, sizeof(projection));
        memcpy(settings.viewToClipMatrixPrev, projection, sizeof(projection));
        for (uint32_t i = 0; i < 16; i += 5)
        {
            settings.worldToViewMatrix[i] = 1.0f;
            settings.worldToViewMatrixPrev[i] = 1.0f;
        }
        settings.resourceSize[0] = kTextureSize;
        settings.resourceSize[1] = kTextureSize;
        settings.rectSize[0] = kClearWidth;
        settings.rectSize[1] = kClearHeight;
        settings.accumulationMode = nrd::AccumulationMode::CLEAR_AND_RESTART;
        result = nrd::SetCommonSettings(*instance, settings);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDClear] SetCommonSettings failed ({}).", static_cast<unsigned>(result));
            return false;
        }

        const nrd::DispatchDesc* dispatches = nullptr;
        uint32_t dispatchCount = 0;
        result = nrd::GetComputeDispatches(*instance, &denoiser.identifier, 1, dispatches, dispatchCount);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDClear] GetComputeDispatches failed ({}).", static_cast<unsigned>(result));
            return false;
        }

        const auto& instanceDesc = *nrd::GetInstanceDesc(*instance);
        const auto* clear = FindOutputClear(instanceDesc, dispatches, dispatchCount);
        if (!clear || !clear->constantBufferData || clear->constantBufferDataSize < kClearConstantBytes
            || clear->gridWidth == 0 || clear->gridHeight == 0)
        {
            LOG_ERROR("[NRDClear] No valid floating-point Clear dispatch for OUT_DIFF_RADIANCE_HITDIST.");
            return false;
        }

        const auto& shader = instanceDesc.pipelines[clear->pipelineIndex].computeShaderDXIL;
        if (!shader.bytecode || shader.size == 0)
        {
            LOG_ERROR("[NRDClear] No DXIL in the SDK's Clear pipeline.");
            return false;
        }

        RHI::ShaderInputList inputs;
        inputs.m_images.emplace_back(kOutputInput, RHI::ShaderInputImageAccess::ReadWrite,
            RHI::ShaderInputImageType::Image2D, 1, instanceDesc.resourcesBaseRegisterIndex,
            instanceDesc.resourcesSpaceIndex).m_stageMask = RHI::ShaderStageMask::Compute;
        for (const auto& field : kClearFields)
        {
            auto& input = inputs.m_constants.emplace_back(field.m_name, field.m_offset, kInt2Bytes,
                instanceDesc.constantBufferRegisterIndex, instanceDesc.constantBufferAndSamplersSpaceIndex);
            input.m_stageMask = RHI::ShaderStageMask::Compute;
        }

        auto layout = factory.CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(inputs);
        if (layout->Finalize() != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Clear layout finalization failed.");
            return false;
        }

        auto compute = factory.CreateShaderStageFunction(RHI::ShaderStage::Compute);
        const auto* dxil = static_cast<const uint8_t*>(shader.bytecode);
        compute->SetByteCode(eastl::vector<uint8_t>(dxil, dxil + shader.size));
        if (compute->Finalize() != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Clear DXIL finalization failed.");
            return false;
        }

        RHI::PipelineStateDescriptorForDispatch descriptor;
        descriptor.m_computeFunction = compute;
        descriptor.m_pipelineLayoutDescriptor = layout;
        auto pso = factory.CreatePipelineState();
        if (pso->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Clear PSO initialization failed.");
            return false;
        }

        eastl::array<uint8_t, kClearConstantBytes> constants;
        memcpy(constants.data(), clear->constantBufferData, constants.size());
        const RHI::DispatchGroupCount groups{clear->gridWidth, clear->gridHeight, 1};
        const Render::ShaderInputBinding outputBinding{instanceDesc.resourcesSpaceIndex, kOutputInput};
        LOG_INFO("[NRDClear] Dispatching {} with {}x{} groups.",
            instanceDesc.pipelines[clear->pipelineIndex].shaderIdentifier, clear->gridWidth, clear->gridHeight);

        return SPARK_COMPUTE_PASS(passes, "NRDClear")
            .Queue(kQueue)
            .ImportPipeline(pso, Render::PassPipelineLayout{layout, {}})
            .Build([output, outputBinding, groups, constants](Render::ComputePassScopes& pass)
            {
                auto& context = *RHI::RHIExecuteContext::Current();
                if (!RHI::IsResourceReady(context, output) || context.Has<Render::PendingSync>(output))
                {
                    return;
                }
                pass.Import(kOutput, output);
                auto scope = pass.Scope();
                scope.WriteImage(kOutput).Bind(outputBinding);
                for (const auto& field : kClearFields)
                {
                    scope.Constant(field.m_name,
                        eastl::span<const uint8_t>(constants.data() + field.m_offset, kInt2Bytes));
                }
                scope.Dispatch(groups);
                scope.Close();
            })
            .Finalize() != Render::NullPass;
    }

    bool CheckPixels(const void* data, uint32_t rowPitch)
    {
        for (uint32_t y = 0; y < kTextureSize; ++y)
        {
            const auto* row = static_cast<const uint8_t*>(data) + y * rowPitch;
            for (uint32_t x = 0; x < kTextureSize; ++x)
            {
                eastl::array<uint16_t, 4> pixel;
                memcpy(pixel.data(), row + x * sizeof(pixel), sizeof(pixel));
                const uint16_t expected = x < kClearWidth && y < kClearHeight ? 0 : kHalfOne;
                if (pixel != eastl::array<uint16_t, 4>{expected, expected, expected, expected})
                {
                    LOG_ERROR("[NRDClear] Pixel ({}, {}) expected all channels 0x{:04x}, got "
                        "[0x{:04x}, 0x{:04x}, 0x{:04x}, 0x{:04x}].",
                        x, y, expected, pixel[0], pixel[1], pixel[2], pixel[3]);
                    return false;
                }
            }
        }
        LOG_INFO("[NRDClear] PASS: {} pixels cleared and {} pixels retained their initial value.",
            kClearWidth * kClearHeight, kTextureSize * kTextureSize - kClearWidth * kClearHeight);
        return true;
    }

    bool ReadbackAndVerify(RHI::Factory& factory, RHI::Image& image, const Render::PendingSync& clearDone)
    {
        auto& device = image.GetDevice();
        RHI::ImageSubresourceLayout footprint;
        image.GetSubresourceLayouts(RHI::ImageSubresourceRange(0, 0, 0, 0), &footprint, nullptr);
        const auto byteCount = footprint.m_bytesPerImage;

        RHI::BufferPoolDescriptor poolDescriptor;
        poolDescriptor.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
        poolDescriptor.m_hostMemoryAccess = RHI::HostMemoryAccess::Read;
        poolDescriptor.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
        auto readbackPool = factory.CreateBufferPool();
        if (readbackPool->Init(device, poolDescriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Readback buffer pool initialization failed.");
            return false;
        }
        auto readback = factory.CreateBuffer();
        if (readbackPool->InitBuffer(RHI::BufferInitRequest(*readback,
                RHI::BufferDescriptor(RHI::BufferBindFlags::CopyWrite, byteCount))) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Readback buffer initialization failed.");
            return false;
        }

        // CopyScope currently handles buffer copies. The readback waits on the
        // renderer's submission fence before copying the image on a graphics queue.
        auto queue = factory.CreateCommandQueue();
        auto recorder = factory.CreateCommandRecorder();
        auto fence = factory.CreateFence();
        if (queue->Init(device, RHI::CommandQueueDescriptor{kQueue}) != RHI::ResultCode::Success
            || recorder->Init(device, RHI::CommandRecorderDescriptor{kQueue}) != RHI::ResultCode::Success
            || fence->Init(device, RHI::FenceState::Reset) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Readback queue, recorder or fence initialization failed.");
            return false;
        }

        auto& commands = *recorder->GetCommandList();
        commands.QueueBarrier(RHI::ConvertToImageCopyRead(image));
        commands.FlushBarriers();
        RHI::CopyImageToBufferDescriptor copy;
        copy.m_sourceImage = &image;
        copy.m_sourceSize = footprint.m_size;
        copy.m_destinationBuffer = readback.get();
        copy.m_destinationBytesPerRow = footprint.m_bytesPerRow;
        copy.m_destinationBytesPerImage = byteCount;
        copy.m_destinationFormat = kFormat;
        commands.Submit(RHI::CopyItem(copy));
        commands.Close();

        RHI::CommandList* lists[] = {&commands};
        if (queue->Wait(*clearDone.m_fence, clearDone.m_fenceValue) != RHI::ResultCode::Success
            || queue->ExecuteCommands(lists) != RHI::ResultCode::Success
            || queue->FlushCommands(*fence) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Readback copy failed.");
            return false;
        }

        RHI::BufferMapResponse mapped;
        if (readbackPool->MapBuffer(RHI::BufferMapRequest(*readback, 0, byteCount), mapped)
                != RHI::ResultCode::Success || !mapped.m_data)
        {
            LOG_ERROR("[NRDClear] Could not map the readback buffer.");
            return false;
        }
        const bool passed = CheckPixels(mapped.m_data, footprint.m_bytesPerRow);
        readbackPool->UnmapBuffer(*readback);
        return passed;
    }

    class NRDClearFeature
    {
    public:
        bool Init(Render::PassContext& passes)
        {
            auto& context = *RHI::RHIExecuteContext::Current();
            auto& rhi = *Service<RHI::RHIInterface>::Get();
            auto descriptor = RHI::ImageDescriptor::Create2D(kImageFlags, kTextureSize, kTextureSize, kFormat);
            descriptor.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
            m_output = RHI::UniqueRHIHandle(RHI::CreateImage(context, kOutput, descriptor));
            if (!RegisterClearPass(passes, *rhi.GetRHIFactory(), *rhi.GetDevice(), m_output.Get()))
            {
                return false;
            }

            // The upload can run over several ticks. Keep its source alive through shutdown.
            static const auto initialPixels = []
            {
                eastl::array<uint16_t, kTextureSize * kTextureSize * 4> pixels;
                pixels.fill(kHalfOne);
                return pixels;
            }();
            RHI::RequestImageUpload(context, m_output.Get(), initialPixels.data(),
                initialPixels.size() * sizeof(uint16_t), RHI::ImageSubresourceRange(descriptor), {}, kFormat);
            return true;
        }

        eastl::optional<bool> VerifyIfSubmitted() const
        {
            auto& context = *RHI::RHIExecuteContext::Current();
            const auto output = m_output.Get();
            if (!context.Valid(output) || context.Has<DeadTag>(output))
            {
                LOG_ERROR("[NRDClear] Output image initialization failed.");
                return false;
            }

            // The renderer stamps PendingSync after a frame that uses this image.
            const auto* submitted = context.TryGet<Render::PendingSync>(output);
            if (!submitted)
            {
                return eastl::nullopt;
            }

            auto* factory = Service<RHI::Factory>::Get();
            auto& image = *context.Get<RHI::Components::Image>(output).m_image;
            return ReadbackAndVerify(*factory, image, *submitted);
        }

    private:
        RHI::UniqueRHIHandle m_output;
    };
}

int main()
{
    auto sys = SandBox::InitRenderGraphApp(640, 480, "NRDClear");
    auto& passes = *Render::PassExecuteContext::Current();
    sys.m_render->SetUpSystemPasses(passes);

    NRDClearFeature feature;
    if (!feature.Init(passes))
    {
        return 1;
    }

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, FrameTime{});
        if (const auto passed = feature.VerifyIfSubmitted())
        {
            return *passed ? 0 : 1;
        }
    }
    return 1;
}
