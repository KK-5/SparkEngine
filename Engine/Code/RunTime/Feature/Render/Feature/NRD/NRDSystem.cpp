#include "NRDSystem.h"

#if SPARK_WITH_NRD
#include <math.h>

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <NRD.h>

#include <Binding/View/ViewBinding.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <RHI/Factory.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/ShaderStageFunction.h>
#include <Tick/FrameTime.h>
#include <View/ViewComponents.h>
#include <View/ViewTags.h>
#endif

namespace Spark::Render
{
#if SPARK_WITH_NRD
    namespace
    {
        constexpr nrd::Identifier kDenoiserId = 1;

        struct DestroyInstance
        {
            void operator()(nrd::Instance* instance) const { nrd::DestroyInstance(*instance); }
        };

        Ptr<RHI::PipelineLayoutDescriptor> CreatePipelineLayout(RHI::Factory& factory, const nrd::InstanceDesc& instance)
        {
            uint32_t maxSrvs = 0;
            uint32_t maxUavs = 0;
            for (uint32_t i = 0; i < instance.pipelinesNum; ++i)
            {
                const auto& pipeline = instance.pipelines[i];
                uint32_t srvCount = 0;
                uint32_t uavCount = 0;
                for (uint32_t j = 0; j < pipeline.resourceRangesNum; ++j)
                {
                    const auto& range = pipeline.resourceRanges[j];
                    switch (range.descriptorType)
                    {
                    case nrd::DescriptorType::TEXTURE:
                        srvCount += range.descriptorsNum;
                        break;
                    case nrd::DescriptorType::STORAGE_TEXTURE:
                        uavCount += range.descriptorsNum;
                        break;
                    default:
                        LOG_ERROR("[NRDSystem] Unsupported descriptor range in {}.", pipeline.shaderIdentifier);
                        return nullptr;
                    }
                }
                maxSrvs = eastl::max(maxSrvs, srvCount);
                maxUavs = eastl::max(maxUavs, uavCount);
            }

            RHI::ShaderInputList inputs;
            if (maxSrvs != 0)
            {
                inputs.m_images.emplace_back(RHI::InputName("NRDTextures"), RHI::ShaderInputImageAccess::Read,
                    RHI::ShaderInputImageType::Image2D, maxSrvs, instance.resourcesBaseRegisterIndex,
                    instance.resourcesSpaceIndex).m_stageMask = RHI::ShaderStageMask::Compute;
            }
            if (maxUavs != 0)
            {
                inputs.m_images.emplace_back(RHI::InputName("NRDStorageTextures"), RHI::ShaderInputImageAccess::ReadWrite,
                    RHI::ShaderInputImageType::Image2D, maxUavs, instance.resourcesBaseRegisterIndex,
                    instance.resourcesSpaceIndex).m_stageMask = RHI::ShaderStageMask::Compute;
            }
            if (instance.constantBufferMaxDataSize != 0)
            {
                // Reserve the CBV for PSO creation. Scope uploads will use the individual SDK fields.
                inputs.m_constants.emplace_back(RHI::InputName("NRDConstants"), 0, instance.constantBufferMaxDataSize,
                    instance.constantBufferRegisterIndex, instance.constantBufferAndSamplersSpaceIndex)
                    .m_stageMask = RHI::ShaderStageMask::Compute;
            }

            auto layout = factory.CreatePipelineLayoutDescriptor();
            layout->AddShaderInputDescriptors(inputs);
            for (uint32_t i = 0; i < instance.samplersNum; ++i)
            {
                const auto sampler = instance.samplers[i];
                if (sampler != nrd::Sampler::NEAREST_CLAMP && sampler != nrd::Sampler::LINEAR_CLAMP)
                {
                    LOG_ERROR("[NRDSystem] Unsupported sampler {}.", static_cast<unsigned>(sampler));
                    return nullptr;
                }
                const bool linear = sampler == nrd::Sampler::LINEAR_CLAMP;
                const auto filter = linear ? RHI::FilterMode::Linear : RHI::FilterMode::Point;
                const auto state = RHI::SamplerState::Create(filter, RHI::FilterMode::Point, RHI::AddressMode::Clamp);
                layout->AddStaticSamplerDescriptor(RHI::ShaderInputStaticSamplerDescriptor(
                    RHI::InputName(linear ? "NRDLinearClamp" : "NRDNearestClamp"), state,
                    instance.samplersBaseRegisterIndex + i, instance.constantBufferAndSamplersSpaceIndex),
                    RHI::ShaderStageMask::Compute);
            }

            if (layout->Finalize() != RHI::ResultCode::Success)
            {
                LOG_ERROR("[NRDSystem] Pipeline layout finalization failed.");
                return nullptr;
            }
            return layout;
        }

        Ptr<RHI::PipelineState> CreateComputePipeline(RHI::Factory& factory, RHI::Device& device,
            const nrd::PipelineDesc& pipeline, const Ptr<RHI::PipelineLayoutDescriptor>& layout)
        {
            const auto& shader = pipeline.computeShaderDXIL;
            if (!shader.bytecode || shader.size == 0)
            {
                LOG_ERROR("[NRDSystem] Missing DXIL for {}.", pipeline.shaderIdentifier);
                return nullptr;
            }

            auto compute = factory.CreateShaderStageFunction(RHI::ShaderStage::Compute);
            const auto* bytecode = static_cast<const uint8_t*>(shader.bytecode);
            compute->SetByteCode(eastl::vector<uint8_t>(bytecode, bytecode + shader.size));
            if (compute->Finalize() != RHI::ResultCode::Success)
            {
                LOG_ERROR("[NRDSystem] Shader finalization failed for {}.", pipeline.shaderIdentifier);
                return nullptr;
            }

            RHI::PipelineStateDescriptorForDispatch descriptor;
            descriptor.m_computeFunction = compute;
            descriptor.m_pipelineLayoutDescriptor = layout;
            auto pso = factory.CreatePipelineState();
            pso->SetName(ObjectName(pipeline.shaderIdentifier));
            if (pso->Init(device, descriptor) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[NRDSystem] PSO initialization failed for {}.", pipeline.shaderIdentifier);
                return nullptr;
            }
            return pso;
        }

        void CopyMatrix(float (&destination)[16], const Math::Matrix4X4& matrix)
        {
            eastl::copy_n(Math::ValuePtr(matrix), 16, destination);
        }

        bool SetViewSettings(const Math::Vector2Int& size, const ViewRect& rect, const Math::Vector2& jitter,
            uint16_t (&resourceSize)[2], uint16_t (&rectSize)[2], float (&cameraJitter)[2])
        {
            constexpr auto maxSize = (eastl::numeric_limits<uint16_t>::max)();
            if (size.x <= 0 || size.y <= 0 || size.x > maxSize || size.y > maxSize)
            {
                return false;
            }

            // The Spark NRD SDK uses zero-origin input and output rectangles.
            const float resourceWidth = static_cast<float>(size.x);
            const float resourceHeight = static_cast<float>(size.y);
            const float width = roundf(resourceWidth * rect.m_maxX);
            const float height = roundf(resourceHeight * rect.m_maxY);
            if (rect.m_minX != 0.0f || rect.m_minY != 0.0f
                || !(width >= 1.0f && width <= resourceWidth && height >= 1.0f && height <= resourceHeight))
            {
                return false;
            }
            resourceSize[0] = static_cast<uint16_t>(size.x);
            resourceSize[1] = static_cast<uint16_t>(size.y);
            rectSize[0] = static_cast<uint16_t>(width);
            rectSize[1] = static_cast<uint16_t>(height);

            // Undo the raster projection offset to get the sample offset in pixel coordinates.
            cameraJitter[0] = -0.5f * jitter.x * resourceWidth;
            cameraJitter[1] = 0.5f * jitter.y * resourceHeight;
            return true;
        }

        bool HasValidResources(const nrd::InstanceDesc& instance, const nrd::DispatchDesc& dispatch)
        {
            const auto& pipeline = instance.pipelines[dispatch.pipelineIndex];
            uint32_t offset = 0;
            for (uint32_t i = 0; i < pipeline.resourceRangesNum; ++i)
            {
                const auto& range = pipeline.resourceRanges[i];
                if (range.descriptorsNum > dispatch.resourcesNum - offset
                    || (range.descriptorsNum != 0 && !dispatch.resources))
                {
                    return false;
                }
                for (uint32_t j = 0; j < range.descriptorsNum; ++j)
                {
                    const auto& resource = dispatch.resources[offset++];
                    if (resource.descriptorType != range.descriptorType || resource.type >= nrd::ResourceType::MAX_NUM
                        || (resource.type == nrd::ResourceType::PERMANENT_POOL && resource.indexInPool >= instance.permanentPoolSize)
                        || (resource.type == nrd::ResourceType::TRANSIENT_POOL && resource.indexInPool >= instance.transientPoolSize))
                    {
                        return false;
                    }
                }
            }
            return offset == dispatch.resourcesNum;
        }
    }

    struct NRDSystem::State
    {
        UniquePtr<nrd::Instance, DestroyInstance> m_instance;
        Ptr<RHI::PipelineLayoutDescriptor> m_layout;
        eastl::vector<Ptr<RHI::PipelineState>> m_pipelines;
        bool m_startupFrameAttempted = false;
    };

    void NRDSystem::Init(RHI::Factory& factory, RHI::Device& device)
    {
        if (m_state)
        {
            return;
        }

        const auto& library = *nrd::GetLibraryDesc();
        if (library.versionMajor != 4 || library.versionMinor != 18)
        {
            LOG_ERROR("[NRDSystem] Expected NRD 4.18, got {}.{}.",
                static_cast<unsigned>(library.versionMajor), static_cast<unsigned>(library.versionMinor));
            return;
        }

        const nrd::DenoiserDesc denoiser{ kDenoiserId, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR };
        nrd::InstanceCreationDesc creation{};
        creation.denoisers = &denoiser;
        creation.denoisersNum = 1;
        nrd::Instance* instance = nullptr;
        const auto result = nrd::CreateInstance(creation, instance);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDSystem] CreateInstance failed ({}).", static_cast<unsigned>(result));
            return;
        }

        auto state = MakeUnique<State>();
        state->m_instance.reset(instance);
        const auto& desc = *nrd::GetInstanceDesc(*instance);
        state->m_layout = CreatePipelineLayout(factory, desc);
        if (!state->m_layout)
        {
            return;
        }
        state->m_pipelines.reserve(desc.pipelinesNum);
        for (uint32_t i = 0; i < desc.pipelinesNum; ++i)
        {
            auto pso = CreateComputePipeline(factory, device, desc.pipelines[i], state->m_layout);
            if (!pso)
            {
                return;
            }
            state->m_pipelines.push_back(eastl::move(pso));
        }
        LOG_INFO("[NRDSystem] Initialized REBLUR_DIFFUSE_SPECULAR with {} PSOs, {} permanent textures, "
            "{} transient textures and a {}-byte maximum constant buffer.",
            desc.pipelinesNum, desc.permanentPoolSize, desc.transientPoolSize, desc.constantBufferMaxDataSize);
        m_state = eastl::move(state);
    }

    void NRDSystem::PrepareFrame(const RHI::RHIContext& rhiContext, const FrameTime& time)
    {
        if (!m_state || m_state->m_startupFrameAttempted)
        {
            return;
        }

        const auto mainViews = rhiContext.GetView<MainViewTag, View, ViewHistory, ViewIndex>(Exclude<DeadTag, ViewInactiveTag>);
        const auto mainView = mainViews.front();
        if (mainView == RHI::NullHandle)
        {
            return;
        }

        const auto& view = rhiContext.Get<View>(mainView);
        const auto& previous = rhiContext.Get<ViewHistory>(mainView);
        nrd::CommonSettings settings{};
        if (!SetViewSettings(view.m_bufferSize, view.m_rect, view.m_jitter,
                settings.resourceSize, settings.rectSize, settings.cameraJitter)
            || !SetViewSettings(previous.m_bufferSize, previous.m_rect, previous.m_jitter,
                settings.resourceSizePrev, settings.rectSizePrev, settings.cameraJitterPrev))
        {
            return;
        }

        CopyMatrix(settings.worldToViewMatrix, view.m_worldToView);
        CopyMatrix(settings.viewToClipMatrix, view.m_viewToClip);
        CopyMatrix(settings.worldToViewMatrixPrev, previous.m_worldToView);
        CopyMatrix(settings.viewToClipMatrixPrev, previous.m_viewToClip);
        settings.timeDeltaBetweenFrames = time.m_realDeltaTime * 1000.0f;
        settings.accumulationMode = nrd::AccumulationMode::CLEAR_AND_RESTART;

        // Prepare the startup list once. History can advance when the graph executes it.
        m_state->m_startupFrameAttempted = true;
        auto& instance = *m_state->m_instance;
        auto result = nrd::SetCommonSettings(instance, settings);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDSystem] SetCommonSettings failed ({}).", static_cast<unsigned>(result));
            return;
        }

        const nrd::DispatchDesc* dispatches = nullptr;
        uint32_t count = 0;
        result = nrd::GetComputeDispatches(instance, &kDenoiserId, 1, dispatches, count);
        if (result != nrd::Result::SUCCESS || !dispatches || count == 0)
        {
            LOG_ERROR("[NRDSystem] Could not prepare the startup dispatches ({}).", static_cast<unsigned>(result));
            return;
        }

        const auto& desc = *nrd::GetInstanceDesc(instance);
        uint32_t clearCount = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& dispatch = dispatches[i];
            if (dispatch.pipelineIndex >= m_state->m_pipelines.size() || !m_state->m_pipelines[dispatch.pipelineIndex]
                || dispatch.gridWidth == 0 || dispatch.gridHeight == 0
                || dispatch.constantBufferDataSize > desc.constantBufferMaxDataSize
                || (dispatch.constantBufferDataSize != 0 && !dispatch.constantBufferData)
                || !HasValidResources(desc, dispatch))
            {
                LOG_ERROR("[NRDSystem] Invalid startup dispatch {} (pipeline {}).", i, dispatch.pipelineIndex);
                return;
            }

            const auto& pipeline = desc.pipelines[dispatch.pipelineIndex];
            if (eastl::string_view(pipeline.shaderIdentifier).starts_with("Clear.cs.hlsl|"))
            {
                ++clearCount;
            }
        }

        LOG_INFO("[NRDSystem] Prepared {} startup dispatches ({} Clear, {} REBLUR) for MainView {}x{}.",
            count, clearCount, count - clearCount, settings.rectSize[0], settings.rectSize[1]);
    }
#else
    struct NRDSystem::State {};

    void NRDSystem::Init(RHI::Factory&, RHI::Device&) {}
    void NRDSystem::PrepareFrame(const RHI::RHIContext&, const FrameTime&) {}
#endif

    NRDSystem::NRDSystem() = default;
    NRDSystem::~NRDSystem() = default;

    void NRDSystem::Shutdown()
    {
        m_state.reset();
    }
}
