#include "NRDSystem.h"

#if SPARK_WITH_NRD
#include "NRDInputPass.h"

#include <math.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <NRD.h>

#include <Binding/View/ViewBinding.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <Pass/ComputePass.h>
#include <RHI/Component/Component.h>
#include <RHI/Factory.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Pipeline/ShaderStageFunction.h>
#include <RHI/Resource/Image/ImagePool.h>
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
        constexpr auto kQueue = RHI::HardwareQueueClass::Graphics;
        constexpr auto kTextureBindFlags = RHI::ImageBindFlags::ShaderReadWrite;
        constexpr uint32_t kInvalidPipeline = ~0u;

        enum class ClearType : uint8_t { Float, Uint, Count };

        constexpr size_t ClearIndex(ClearType type) { return static_cast<size_t>(type); }
        constexpr size_t kClearTypeCount = ClearIndex(ClearType::Count);

        const RHI::InputName kClearOutput("gOut");
        constexpr uint32_t kClearFieldBytes = 2 * sizeof(int32_t);
        constexpr uint32_t kClearConstantBytes = 2 * kClearFieldBytes;

        struct ClearField
        {
            RHI::InputName m_name;
            uint32_t m_offset;
        };

        // NRD 4.18 Clear reads these two packed int2 fields.
        const ClearField kClearFields[] = {
            { RHI::InputName("gRectSize"), 0 },
            { RHI::InputName("gDispatchOutputRectOrigin"), kClearFieldBytes },
        };

        ClearType GetClearType(eastl::string_view shaderIdentifier)
        {
            if (shaderIdentifier == "Clear.cs.hlsl|FLOAT=1")
            {
                return ClearType::Float;
            }
            if (shaderIdentifier == "Clear.cs.hlsl|FLOAT=0")
            {
                return ClearType::Uint;
            }
            return ClearType::Count;
        }

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

        Ptr<RHI::PipelineLayoutDescriptor> CreateClearPipelineLayout(RHI::Factory& factory, const nrd::InstanceDesc& instance)
        {
            RHI::ShaderInputList inputs;
            inputs.m_images.emplace_back(kClearOutput, RHI::ShaderInputImageAccess::ReadWrite,
                RHI::ShaderInputImageType::Image2D, 1, instance.resourcesBaseRegisterIndex,
                instance.resourcesSpaceIndex).m_stageMask = RHI::ShaderStageMask::Compute;
            for (const auto& field : kClearFields)
            {
                inputs.m_constants.emplace_back(field.m_name, field.m_offset, kClearFieldBytes,
                    instance.constantBufferRegisterIndex, instance.constantBufferAndSamplersSpaceIndex)
                    .m_stageMask = RHI::ShaderStageMask::Compute;
            }

            auto layout = factory.CreatePipelineLayoutDescriptor();
            layout->AddShaderInputDescriptors(inputs);
            if (layout->Finalize() != RHI::ResultCode::Success)
            {
                LOG_ERROR("[NRDSystem] Clear pipeline layout finalization failed.");
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

        RHI::Format GetTextureFormat(nrd::Format format)
        {
            switch (format)
            {
            case nrd::Format::R8_UNORM: return RHI::Format::R8_UNORM;
            case nrd::Format::RG8_UNORM: return RHI::Format::R8G8_UNORM;
            case nrd::Format::RGBA8_UNORM: return RHI::Format::R8G8B8A8_UNORM;
            case nrd::Format::RGBA8_SNORM: return RHI::Format::R8G8B8A8_SNORM;
            case nrd::Format::R16_UINT: return RHI::Format::R16_UINT;
            case nrd::Format::R16_SFLOAT: return RHI::Format::R16_FLOAT;
            case nrd::Format::RGBA16_UNORM: return RHI::Format::R16G16B16A16_UNORM;
            case nrd::Format::RGBA16_SFLOAT: return RHI::Format::R16G16B16A16_FLOAT;
            case nrd::Format::R32_UINT: return RHI::Format::R32_UINT;
            case nrd::Format::R32_SFLOAT: return RHI::Format::R32_FLOAT;
            case nrd::Format::R10_G10_B10_A2_UNORM: return RHI::Format::R10G10B10A2_UNORM;
            default: return RHI::Format::Unknown;
            }
        }

        RHI::Format GetNormalRoughnessFormat(nrd::NormalEncoding encoding)
        {
            switch (encoding)
            {
            case nrd::NormalEncoding::RGBA8_UNORM: return GetTextureFormat(nrd::Format::RGBA8_UNORM);
            case nrd::NormalEncoding::RGBA8_SNORM: return GetTextureFormat(nrd::Format::RGBA8_SNORM);
            case nrd::NormalEncoding::R10_G10_B10_A2_UNORM: return GetTextureFormat(nrd::Format::R10_G10_B10_A2_UNORM);
            case nrd::NormalEncoding::RGBA16_UNORM: return GetTextureFormat(nrd::Format::RGBA16_UNORM);
            case nrd::NormalEncoding::RGBA16_SNORM: return GetTextureFormat(nrd::Format::RGBA16_SFLOAT);
            default: return RHI::Format::Unknown;
            }
        }

        RHI::ImageDescriptor CreateTextureDescriptor(uint32_t width, uint32_t height, RHI::Format format)
        {
            auto desc = RHI::ImageDescriptor::Create2D(kTextureBindFlags, width, height, format);
            desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
            return desc;
        }

        struct GraphTexture
        {
            RHI::AttachmentId m_name;
            RHI::ImageDescriptor m_descriptor;
            // Permanent textures have a backing to import. The graph allocates the others.
            RHI::RHIHandle m_resource = RHI::NullHandle;
        };

        bool DescribeTexturePool(eastl::vector<GraphTexture>& pool, const char* namePrefix,
            const nrd::TextureDesc* textures, uint32_t count, uint32_t width, uint32_t height)
        {
            pool.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& texture = textures[i];
                const auto format = GetTextureFormat(texture.format);
                const uint32_t factor = texture.downsampleFactor;
                if (format == RHI::Format::Unknown || factor == 0)
                {
                    LOG_ERROR("[NRDSystem] Unsupported texture descriptor for {} {} (format {}, downsample {}).",
                        namePrefix, i, static_cast<unsigned>(texture.format), factor);
                    return false;
                }

                const eastl::string name(eastl::string::CtorSprintf(), "%s %u", namePrefix, i);
                pool.push_back(GraphTexture{ RHI::AttachmentId(name.c_str()), CreateTextureDescriptor(
                    (width + factor - 1) / factor, (height + factor - 1) / factor, format) });
            }
            return true;
        }

        struct ClearDispatch
        {
            const GraphTexture* m_target;
            RHI::DispatchGroupCount m_groups;
            eastl::array<uint8_t, kClearConstantBytes> m_constants;
        };

        struct StartupClears
        {
            bool DescribeTextures(const nrd::InstanceDesc& instance, uint32_t width, uint32_t height)
            {
                const auto fullSize = CreateTextureDescriptor(width, height, RHI::Format::R16G16B16A16_FLOAT);
                m_diffuseOutput = { RHI::AttachmentId("NRDDiffuseOutput"), fullSize };
                m_specularOutput = { RHI::AttachmentId("NRDSpecularOutput"), fullSize };
                return DescribeTexturePool(m_permanent, "NRD Permanent", instance.permanentPool, instance.permanentPoolSize,
                           width, height)
                    && DescribeTexturePool(m_transient, "NRD Transient", instance.transientPool, instance.transientPoolSize,
                           width, height);
            }

            bool AllocatePermanentTextures(RHI::RHIContext& rhiContext, RHI::Factory& factory, RHI::ImagePool& pool)
            {
                for (auto& texture : m_permanent)
                {
                    auto image = factory.CreateImage();
                    image->SetName(texture.m_name);
                    RHI::ImageInitRequest request;
                    request.m_image = image.get();
                    request.m_descriptor = texture.m_descriptor;
                    if (pool.InitImage(request) != RHI::ResultCode::Success)
                    {
                        LOG_ERROR("[NRDSystem] Could not initialize {}.", texture.m_name.GetCStr());
                        ReleasePermanentTextures(rhiContext);
                        return false;
                    }

                    const auto resource = rhiContext.CreateEntity();
                    rhiContext.Add<RHI::ResourceName>(resource, RHI::ResourceName{ texture.m_name });
                    rhiContext.Add<RHI::Components::Image>(resource, RHI::Components::Image{ eastl::move(image) });
                    texture.m_resource = resource;
                }
                return true;
            }

            void ReleasePermanentTextures(RHI::RHIContext& rhiContext)
            {
                for (auto& texture : m_permanent)
                {
                    if (rhiContext.Valid(texture.m_resource))
                    {
                        rhiContext.DestoryEntity(texture.m_resource);
                    }
                    texture.m_resource = RHI::NullHandle;
                }
            }

            const GraphTexture* FindTexture(const nrd::ResourceDesc& resource) const
            {
                switch (resource.type)
                {
                case nrd::ResourceType::PERMANENT_POOL: return &m_permanent[resource.indexInPool];
                case nrd::ResourceType::TRANSIENT_POOL: return &m_transient[resource.indexInPool];
                case nrd::ResourceType::IN_MV: return &m_motion;
                case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return &m_diffuseOutput;
                case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return &m_specularOutput;
                default: return nullptr;
                }
            }

            bool AddDispatch(ClearType type, const nrd::DispatchDesc& dispatch)
            {
                if (dispatch.resourcesNum != 1 || dispatch.resources[0].descriptorType != nrd::DescriptorType::STORAGE_TEXTURE
                    || dispatch.constantBufferDataSize < kClearConstantBytes)
                {
                    return false;
                }
                const auto& resource = dispatch.resources[0];
                const auto* target = FindTexture(resource);
                if (!target)
                {
                    return false;
                }

                ClearDispatch clear{ target, RHI::DispatchGroupCount{ dispatch.gridWidth, dispatch.gridHeight, 1 }, {} };
                eastl::copy_n(dispatch.constantBufferData, clear.m_constants.size(), clear.m_constants.begin());
                m_pendingDispatches[ClearIndex(type)].push_back(clear);
                ++m_dispatchCount;
                if (!m_completionTexture && resource.type == nrd::ResourceType::PERMANENT_POOL)
                {
                    m_completionTexture = target;
                }
                return true;
            }

            eastl::vector<GraphTexture> m_permanent;
            eastl::vector<GraphTexture> m_transient;
            GraphTexture m_motion{ NRDInputs::Motion, {} };
            GraphTexture m_diffuseOutput;
            GraphTexture m_specularOutput;
            eastl::array<eastl::vector<ClearDispatch>, kClearTypeCount> m_pendingDispatches;
            uint32_t m_dispatchCount = 0;
            const GraphTexture* m_completionTexture = nullptr;
            bool m_reportedCompletion = false;
        };
    }

    struct NRDSystem::State
    {
        explicit State(RHI::Factory& factory) : m_factory(factory) {}

        RHI::Factory& m_factory;
        UniquePtr<nrd::Instance, DestroyInstance> m_instance;
        nrd::ReblurSettings m_reblurSettings;
        Ptr<Resource::ShaderAsset> m_inputShader;
        RHI::Format m_normalRoughnessFormat = RHI::Format::Unknown;
        Ptr<RHI::PipelineLayoutDescriptor> m_layout;
        Ptr<RHI::PipelineLayoutDescriptor> m_clearLayout;
        eastl::vector<Ptr<RHI::PipelineState>> m_pipelines;
        eastl::array<uint32_t, kClearTypeCount> m_clearPipelines{ kInvalidPipeline, kInvalidPipeline };
        Ptr<RHI::ImagePool> m_permanentPool;
        UniquePtr<StartupClears> m_startupClears;
        bool m_startupFrameAttempted = false;

        void BuildClear(ClearType type, ComputePassScopes& pass);
        void UpdateStartupClearStatus(const RHI::RHIContext& rhiContext);
    };

    void NRDSystem::State::BuildClear(ClearType type, ComputePassScopes& pass)
    {
        if (!m_startupClears)
        {
            return;
        }
        auto& pending = m_startupClears->m_pendingDispatches[ClearIndex(type)];
        if (pending.empty())
        {
            return;
        }

        for (const auto& clear : pending)
        {
            const auto& texture = *clear.m_target;
            if (&texture == &m_startupClears->m_motion)
            {
                // NRDInputPass declares this attachment after the startup Clear passes.
                continue;
            }
            if (texture.m_resource != RHI::NullHandle)
            {
                pass.Import(texture.m_name, texture.m_resource);
            }
            else
            {
                pass.CreateImage(texture.m_name, texture.m_descriptor);
            }
        }

        const auto& desc = *nrd::GetInstanceDesc(*m_instance);
        const ShaderInputBinding outputBinding{ desc.resourcesSpaceIndex, kClearOutput };
        for (const auto& clear : pending)
        {
            auto scope = pass.Scope();
            scope.WriteImage(clear.m_target->m_name).Bind(outputBinding);
            for (const auto& field : kClearFields)
            {
                scope.Constant(field.m_name,
                    eastl::span<const uint8_t>(clear.m_constants.data() + field.m_offset, kClearFieldBytes));
            }
            scope.Dispatch(clear.m_groups);
            scope.Close();
        }
        pending.clear();
    }

    void NRDSystem::State::UpdateStartupClearStatus(const RHI::RHIContext& rhiContext)
    {
        auto& startup = *m_startupClears;
        if (startup.m_reportedCompletion)
        {
            return;
        }
        for (const auto& pending : startup.m_pendingDispatches)
        {
            if (!pending.empty())
            {
                return;
            }
        }

        // Both Clear passes use Graphics. The graph stamps this imported image with that
        // queue's frame-end fence, which covers both passes and their transient writes.
        const auto* submitted = rhiContext.TryGet<PendingSync>(startup.m_completionTexture->m_resource);
        if (submitted && submitted->m_fence && submitted->m_fence->GetCompletedValue() >= submitted->m_fenceValue)
        {
            LOG_INFO("[NRDSystem] Render Graph completed {} startup Clear dispatches.", startup.m_dispatchCount);
            startup.m_reportedCompletion = true;
        }
    }

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

        auto state = MakeUnique<State>(factory);
        state->m_normalRoughnessFormat = GetNormalRoughnessFormat(library.normalEncoding);
        if (state->m_normalRoughnessFormat == RHI::Format::Unknown)
        {
            LOG_ERROR("[NRDSystem] Unsupported normal encoding ({}).", static_cast<unsigned>(library.normalEncoding));
            return;
        }
        state->m_inputShader = NRDInputPass::LoadShader();
        if (!state->m_inputShader)
        {
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

        state->m_instance.reset(instance);
        const auto settingsResult = nrd::SetDenoiserSettings(*instance, kDenoiserId, &state->m_reblurSettings);
        if (settingsResult != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDSystem] SetDenoiserSettings failed ({}).", static_cast<unsigned>(settingsResult));
            return;
        }

        const auto& desc = *nrd::GetInstanceDesc(*instance);
        state->m_layout = CreatePipelineLayout(factory, desc);
        state->m_clearLayout = CreateClearPipelineLayout(factory, desc);
        if (!state->m_layout || !state->m_clearLayout)
        {
            return;
        }
        state->m_pipelines.reserve(desc.pipelinesNum);
        for (uint32_t i = 0; i < desc.pipelinesNum; ++i)
        {
            const auto& pipeline = desc.pipelines[i];
            const auto clearType = GetClearType(pipeline.shaderIdentifier);
            const auto& layout = clearType == ClearType::Count ? state->m_layout : state->m_clearLayout;
            auto pso = CreateComputePipeline(factory, device, pipeline, layout);
            if (!pso)
            {
                return;
            }
            if (clearType != ClearType::Count)
            {
                state->m_clearPipelines[ClearIndex(clearType)] = i;
            }
            state->m_pipelines.push_back(eastl::move(pso));
        }
        if (eastl::find(state->m_clearPipelines.begin(), state->m_clearPipelines.end(), kInvalidPipeline)
            != state->m_clearPipelines.end())
        {
            LOG_ERROR("[NRDSystem] The SDK is missing a Clear shader variant.");
            return;
        }

        RHI::ImagePoolDescriptor poolDescriptor;
        poolDescriptor.m_bindFlags = kTextureBindFlags;
        state->m_permanentPool = factory.CreateImagePool();
        state->m_permanentPool->SetName(ObjectName("NRD Permanent Pool"));
        if (state->m_permanentPool->Init(device, poolDescriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[NRDSystem] Permanent image pool initialization failed.");
            return;
        }

        LOG_INFO("[NRDSystem] Initialized REBLUR_DIFFUSE_SPECULAR with {} PSOs, {} permanent textures, "
            "{} transient textures and a {}-byte maximum constant buffer.",
            desc.pipelinesNum, desc.permanentPoolSize, desc.transientPoolSize, desc.constantBufferMaxDataSize);
        m_state = eastl::move(state);
    }

    void NRDSystem::SetUp(PassContext& passes)
    {
        if (!m_state)
        {
            return;
        }

        const auto floatPipeline = m_state->m_clearPipelines[ClearIndex(ClearType::Float)];
        SPARK_COMPUTE_PASS(passes, "NRDClearFloat")
            .Queue(kQueue)
            .ImportPipeline(m_state->m_pipelines[floatPipeline], PassPipelineLayout{ m_state->m_clearLayout, {} })
            .Build([this](ComputePassScopes& pass) { m_state->BuildClear(ClearType::Float, pass); })
            .Finalize();

        const auto uintPipeline = m_state->m_clearPipelines[ClearIndex(ClearType::Uint)];
        SPARK_COMPUTE_PASS(passes, "NRDClearUint")
            .Queue(kQueue)
            .ImportPipeline(m_state->m_pipelines[uintPipeline], PassPipelineLayout{ m_state->m_clearLayout, {} })
            .Build([this](ComputePassScopes& pass) { m_state->BuildClear(ClearType::Uint, pass); })
            .Finalize();

        const auto& hitDistance = m_state->m_reblurSettings.hitDistanceParameters;
        NRDInputPass::SetUp(passes, m_state->m_inputShader, m_state->m_normalRoughnessFormat,
            { hitDistance.A, hitDistance.B, hitDistance.C });
    }

    void NRDSystem::PrepareFrame(RHI::RHIContext& rhiContext, const FrameTime& time)
    {
        if (!m_state)
        {
            return;
        }
        if (m_state->m_startupClears)
        {
            m_state->UpdateStartupClearStatus(rhiContext);
            return;
        }
        if (m_state->m_startupFrameAttempted)
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

        // Prepare the startup Clear list once. REBLUR will advance history when connected.
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
        auto startup = MakeUnique<StartupClears>();
        if (!startup->DescribeTextures(desc, settings.resourceSize[0], settings.resourceSize[1]))
        {
            return;
        }

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
            const auto clearType = GetClearType(pipeline.shaderIdentifier);
            if (clearType != ClearType::Count)
            {
                if (!startup->AddDispatch(clearType, dispatch))
                {
                    LOG_ERROR("[NRDSystem] Could not map startup Clear dispatch {} (pipeline {}).", i, dispatch.pipelineIndex);
                    return;
                }
            }
        }

        if (!startup->m_completionTexture)
        {
            LOG_ERROR("[NRDSystem] The startup list did not include a permanent texture Clear.");
            return;
        }

        if (!startup->AllocatePermanentTextures(rhiContext, m_state->m_factory, *m_state->m_permanentPool))
        {
            return;
        }
        const auto clearCount = startup->m_dispatchCount;
        m_state->m_startupClears = eastl::move(startup);
        LOG_INFO("[NRDSystem] Prepared {} startup dispatches ({} Clear, {} REBLUR) for MainView {}x{}.",
            count, clearCount, count - clearCount, settings.rectSize[0], settings.rectSize[1]);
    }

    void NRDSystem::Shutdown(RHI::RHIContext& rhiContext)
    {
        if (m_state && m_state->m_startupClears)
        {
            m_state->m_startupClears->ReleasePermanentTextures(rhiContext);
        }
        m_state.reset();
    }
#else
    struct NRDSystem::State {};

    void NRDSystem::Init(RHI::Factory&, RHI::Device&) {}
    void NRDSystem::SetUp(PassContext&) {}
    void NRDSystem::PrepareFrame(RHI::RHIContext&, const FrameTime&) {}
    void NRDSystem::Shutdown(RHI::RHIContext&) {}
#endif

    NRDSystem::NRDSystem() = default;
    NRDSystem::~NRDSystem() = default;
}
