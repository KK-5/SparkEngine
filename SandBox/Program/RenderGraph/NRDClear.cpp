#include <Base.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Device/Device.h>
#include <RHI/Factory.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Pipeline/ShaderStageFunction.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

#include <Pass/ComputePass.h>
#include <Pass/CopyPass.h>
#include <Pass/RenderPass.h>

#include "../Common/RenderGraphUtil.h"

#include <EASTL/array.h>
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
    constexpr auto kImageFlags = RHI::ImageBindFlags::Color
        | RHI::ImageBindFlags::ShaderWrite | RHI::ImageBindFlags::CopyRead;
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
    const RHI::AttachmentId kReadback{"NRDClearReadback"};

    void RegisterInitializePass(Render::PassContext& passes, const RHI::ImageDescriptor& outputDesc)
    {
        SPARK_RENDER_PASS(passes, "NRDClearInitialize")
            .Queue(kQueue)
            .Build([outputDesc](Render::RenderPassScopes& pass)
            {
                pass.CreateImage(kOutput, outputDesc);
                const RHI::AttachmentLoadStoreAction initialize(
                    RHI::ClearValue::CreateVector4Float(1.0f, 1.0f, 1.0f, 1.0f), RHI::AttachmentLoadAction::Clear);
                auto scope = pass.Scope();
                scope.RenderTarget(kOutput, initialize);
                scope.Close();
            })
            // The attachment load action performs all the work of this pass.
            .Execute([](Render::ExecuteWork&, Render::RenderGraphExecuter&) {})
            .Finalize();
    }

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

    void RegisterClearPass(Render::PassContext& passes, RHI::Factory& factory, RHI::Device& device)
    {
        const auto& library = *nrd::GetLibraryDesc();
        if (library.versionMajor != 4 || library.versionMinor != 18)
        {
            LOG_ERROR("[NRDClear] Expected NRD 4.18, got {}.{}.",
                static_cast<unsigned>(library.versionMajor), static_cast<unsigned>(library.versionMinor));
            return;
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
            return;
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
            return;
        }

        const nrd::DispatchDesc* dispatches = nullptr;
        uint32_t dispatchCount = 0;
        result = nrd::GetComputeDispatches(*instance, &denoiser.identifier, 1, dispatches, dispatchCount);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDClear] GetComputeDispatches failed ({}).", static_cast<unsigned>(result));
            return;
        }

        const auto& instanceDesc = *nrd::GetInstanceDesc(*instance);
        const auto* clear = FindOutputClear(instanceDesc, dispatches, dispatchCount);
        if (!clear || !clear->constantBufferData || clear->constantBufferDataSize < kClearConstantBytes
            || clear->gridWidth == 0 || clear->gridHeight == 0)
        {
            LOG_ERROR("[NRDClear] No valid floating-point Clear dispatch for OUT_DIFF_RADIANCE_HITDIST.");
            return;
        }

        const auto& shader = instanceDesc.pipelines[clear->pipelineIndex].computeShaderDXIL;
        if (!shader.bytecode || shader.size == 0)
        {
            LOG_ERROR("[NRDClear] No DXIL in the SDK's Clear pipeline.");
            return;
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
            return;
        }

        auto compute = factory.CreateShaderStageFunction(RHI::ShaderStage::Compute);
        const auto* dxil = static_cast<const uint8_t*>(shader.bytecode);
        compute->SetByteCode(eastl::vector<uint8_t>(dxil, dxil + shader.size));
        if (compute->Finalize() != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Clear DXIL finalization failed.");
            return;
        }

        RHI::PipelineStateDescriptorForDispatch descriptor;
        descriptor.m_computeFunction = compute;
        descriptor.m_pipelineLayoutDescriptor = layout;
        auto pso = factory.CreatePipelineState();
        if (pso->Init(device, descriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDClear] Clear PSO initialization failed.");
            return;
        }

        eastl::array<uint8_t, kClearConstantBytes> constants;
        memcpy(constants.data(), clear->constantBufferData, constants.size());
        const RHI::DispatchGroupCount groups{clear->gridWidth, clear->gridHeight, 1};
        const Render::ShaderInputBinding outputBinding{instanceDesc.resourcesSpaceIndex, kOutputInput};
        LOG_INFO("[NRDClear] Dispatching {} with {}x{} groups.",
            instanceDesc.pipelines[clear->pipelineIndex].shaderIdentifier, clear->gridWidth, clear->gridHeight);

        SPARK_COMPUTE_PASS(passes, "NRDClear")
            .Queue(kQueue)
            .ImportPipeline(pso, Render::PassPipelineLayout{layout, {}})
            .Build([outputBinding, groups, constants](Render::ComputePassScopes& pass)
            {
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
            .Finalize();
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

    class ReadbackBuffer
    {
    public:
        ReadbackBuffer(RHI::RHIContext& context, const RHI::ImageSubresourceLayout& layout)
            : m_context(context), m_layout(layout)
        {
        }

        ReadbackBuffer(const ReadbackBuffer&) = delete;
        ReadbackBuffer& operator=(const ReadbackBuffer&) = delete;

        ~ReadbackBuffer()
        {
            if (m_resource != RHI::NullHandle)
            {
                m_context.DestoryEntity(m_resource);
            }
        }

        void Init(RHI::Factory& factory, RHI::Device& device)
        {
            RHI::BufferPoolDescriptor poolDesc;
            poolDesc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
            poolDesc.m_hostMemoryAccess = RHI::HostMemoryAccess::Read;
            poolDesc.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
            poolDesc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
            m_pool = factory.CreateBufferPool();
            if (m_pool->Init(device, poolDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[NRDClear] Readback buffer pool initialization failed.");
                return;
            }

            auto buffer = factory.CreateBuffer();
            RHI::BufferDescriptor bufferDesc(RHI::BufferBindFlags::CopyWrite, m_layout.m_bytesPerImage);
            bufferDesc.m_sharedQueueMask = poolDesc.m_sharedQueueMask;
            if (m_pool->InitBuffer(RHI::BufferInitRequest(*buffer, bufferDesc)) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[NRDClear] Readback buffer initialization failed.");
                return;
            }

            m_resource = m_context.CreateEntity();
            m_context.Add<RHI::ResourceName>(m_resource, RHI::ResourceName{ kReadback });
            m_context.Add<RHI::Components::Buffer>(m_resource, RHI::Components::Buffer{ eastl::move(buffer) });
        }

        void RegisterPass(Render::PassContext& passes) const
        {
            if (m_resource == RHI::NullHandle)
            {
                return;
            }

            SPARK_COPY_PASS(passes, "NRDClearReadback")
                .Queue(kQueue)
                .Build([resource = m_resource](Render::CopyPassScopes& pass)
                {
                    pass.Import(kReadback, resource);
                    auto scope = pass.Scope();
                    auto view = RHI::ImageViewDescriptor::Create(RHI::Format::Unknown, 0, 0, 0, 0);
                    view.m_aspectFlags = RHI::ImageAspectFlags::Color;
                    auto source = scope.CopyImageSource(kOutput);
                    source.View(view);
                    const auto destination = scope.CopyDestination(kReadback);
                    scope.CopyImageToBuffer(source, destination);
                    scope.Close();
                })
                .Finalize();
        }

        bool Verify() const
        {
            if (m_resource == RHI::NullHandle)
            {
                return false;
            }

            // This test runs a single frame. Its readback is covered by the graph's queue fence.
            const auto* submitted = m_context.TryGet<Render::PendingSync>(m_resource);
            if (!submitted || !submitted->m_fence
                || submitted->m_fence->WaitOnCpu() != RHI::ResultCode::Success
                || submitted->m_fence->GetCompletedValue() < submitted->m_fenceValue)
            {
                LOG_ERROR("[NRDClear] The graph did not complete the readback copy.");
                return false;
            }

            auto& buffer = *m_context.Get<RHI::Components::Buffer>(m_resource).m_buffer;
            RHI::BufferMapResponse mapped;
            if (m_pool->MapBuffer(RHI::BufferMapRequest(buffer, 0, m_layout.m_bytesPerImage), mapped)
                    != RHI::ResultCode::Success || !mapped.m_data)
            {
                LOG_ERROR("[NRDClear] Could not map the readback buffer.");
                return false;
            }
            const bool passed = CheckPixels(mapped.m_data, m_layout.m_bytesPerRow);
            m_pool->UnmapBuffer(buffer);
            return passed;
        }

    private:
        RHI::RHIContext& m_context;
        RHI::ImageSubresourceLayout m_layout;
        Ptr<RHI::BufferPool> m_pool;
        RHI::RHIHandle m_resource = RHI::NullHandle;
    };
}

int main()
{
    auto sys = SandBox::InitRenderGraphApp(640, 480, "NRDClear");
    auto& passes = *Render::PassExecuteContext::Current();
    sys.m_render->SetUpSystemPasses(passes);

    auto& rhi = *Service<RHI::RHIInterface>::Get();
    auto& factory = *rhi.GetRHIFactory();
    auto& device = *rhi.GetDevice();
    auto outputDesc = RHI::ImageDescriptor::Create2D(kImageFlags, kTextureSize, kTextureSize, kFormat);
    outputDesc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
    const auto layout = RHI::GetRowAlignedImageSubresourceLayout(outputDesc, {}, device.GetLimits());
    ReadbackBuffer readback(*RHI::RHIExecuteContext::Current(), layout);

    readback.Init(factory, device);
    RegisterInitializePass(passes, outputDesc);
    RegisterClearPass(passes, factory, device);
    readback.RegisterPass(passes);

    if (sys.m_window->ShouldClose())
    {
        return 1;
    }

    TickBus::Broadcast(&TickBus::Events::OnTick, FrameTime{});
    return readback.Verify() ? 0 : 1;
}
