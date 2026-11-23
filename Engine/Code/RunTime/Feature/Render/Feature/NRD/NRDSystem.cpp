#include "NRDSystem.h"

#if SPARK_WITH_NRD
#include "NRDInputPass.h"
#include "NRDReblurConstants.h"
#include "NRDViewOutput.h"

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
#include <Feature/SceneTextures/SceneTextures.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <Pass/ComputePass.h>
#include <RHI/Component/Component.h>
#include <RHI/Factory.h>
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
        constexpr float kHistoryTimeSeconds = 0.10f;
        constexpr float kFrameTimeSmoothingSeconds = 0.20f;
        constexpr auto kQueue = RHI::HardwareQueueClass::Graphics;
        constexpr auto kTextureBindFlags = RHI::ImageBindFlags::ShaderReadWrite;
        constexpr uint32_t kInvalidPipeline = ~0u;

        enum class ClearType : uint8_t { Float, Uint, Count };

        constexpr size_t ClearIndex(ClearType type) { return static_cast<size_t>(type); }
        constexpr size_t kClearTypeCount = ClearIndex(ClearType::Count);

        const RHI::InputName kClearOutput("gOut");
        constexpr uint32_t kClearFieldBytes = 2 * sizeof(int32_t);
        const RHI::InputName kTextures("NRDTextures");
        const RHI::InputName kStorageTextures("NRDStorageTextures");

        enum class ReblurStage : uint8_t
        {
            ClassifyTiles, PrePass, TemporalAccumulation, HistoryFix, Blur, PostBlur, TemporalStabilization, Count
        };

        constexpr size_t StageIndex(ReblurStage stage) { return static_cast<size_t>(stage); }
        constexpr size_t kReblurStageCount = StageIndex(ReblurStage::Count);
        const eastl::array<eastl::string_view, kReblurStageCount> kReblurShaders{
            "REBLUR_ClassifyTiles.cs.hlsl",
            "REBLUR_PrePass.cs.hlsl",
            "REBLUR_TemporalAccumulation.cs.hlsl",
            "REBLUR_HistoryFix.cs.hlsl",
            "REBLUR_Blur.cs.hlsl",
            "REBLUR_PostBlur.cs.hlsl",
            "REBLUR_TemporalStabilization.cs.hlsl",
        };

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
                inputs.m_images.emplace_back(kTextures, RHI::ShaderInputImageAccess::Read,
                    RHI::ShaderInputImageType::Image2D, maxSrvs, instance.resourcesBaseRegisterIndex,
                    instance.resourcesSpaceIndex).m_stageMask = RHI::ShaderStageMask::Compute;
            }
            if (maxUavs != 0)
            {
                inputs.m_images.emplace_back(kStorageTextures, RHI::ShaderInputImageAccess::ReadWrite,
                    RHI::ShaderInputImageType::Image2D, maxUavs, instance.resourcesBaseRegisterIndex,
                    instance.resourcesSpaceIndex).m_stageMask = RHI::ShaderStageMask::Compute;
            }
            uint32_t offset = 0;
            for (const auto& field : NRDReblur::kSharedFields)
            {
                inputs.m_constants.emplace_back(RHI::InputName(field.m_name), offset, field.m_byteCount,
                    instance.constantBufferRegisterIndex, instance.constantBufferAndSamplersSpaceIndex)
                    .m_stageMask = RHI::ShaderStageMask::Compute;
                offset += field.m_byteCount;
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

        struct FrameTextures
        {
            bool DescribeTextures(const nrd::InstanceDesc& instance, uint32_t width, uint32_t height)
            {
                m_width = width;
                m_height = height;
                const auto fullSize = CreateTextureDescriptor(width, height, RHI::Format::R16G16B16A16_FLOAT);
                m_diffuseOutput = { NRDOutputs::Diffuse, fullSize };
                m_specularOutput = { NRDOutputs::Specular, fullSize };
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

            void Declare(ComputePassScopes& pass) const
            {
                for (const auto& texture : m_permanent)
                {
                    pass.Import(texture.m_name, texture.m_resource);
                }
                for (const auto& texture : m_transient)
                {
                    pass.CreateImage(texture.m_name, texture.m_descriptor);
                }
                pass.CreateImage(m_diffuseOutput.m_name, m_diffuseOutput.m_descriptor);
                pass.CreateImage(m_specularOutput.m_name, m_specularOutput.m_descriptor);
            }

            const RHI::AttachmentId* FindAttachment(const nrd::ResourceDesc& resource) const
            {
                switch (resource.type)
                {
                case nrd::ResourceType::PERMANENT_POOL: return &m_permanent[resource.indexInPool].m_name;
                case nrd::ResourceType::TRANSIENT_POOL: return &m_transient[resource.indexInPool].m_name;
                case nrd::ResourceType::IN_MV: return &NRDInputs::Motion;
                case nrd::ResourceType::IN_NORMAL_ROUGHNESS: return &NRDInputs::NormalRoughness;
                case nrd::ResourceType::IN_VIEWZ: return &SceneTextures::DirectLighting::ViewZName();
                case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: return &NRDInputs::Diffuse;
                case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST: return &NRDInputs::Specular;
                case nrd::ResourceType::IN_DIFF_CONFIDENCE:
                case nrd::ResourceType::IN_SPEC_CONFIDENCE: return &NRDInputs::HistoryConfidence;
                case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return &m_diffuseOutput.m_name;
                case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return &m_specularOutput.m_name;
                default: return nullptr;
                }
            }

            uint32_t m_width = 0;
            uint32_t m_height = 0;
            eastl::vector<GraphTexture> m_permanent;
            eastl::vector<GraphTexture> m_transient;
            GraphTexture m_diffuseOutput;
            GraphTexture m_specularOutput;
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
        eastl::array<uint32_t, kReblurStageCount> m_reblurPipelines;
        Ptr<RHI::ImagePool> m_permanentPool;
        UniquePtr<FrameTextures> m_textures;
        // SDK-owned dispatch data is valid until the next GetComputeDispatches call.
        eastl::span<const nrd::DispatchDesc> m_dispatches;
        RHI::RHIHandle m_lastView = RHI::NullHandle;
        uint64_t m_lastFrameNumber = 0;
        float m_smoothedFrameTime = 1.0f / 60.0f;

        void BuildClear(ClearType type, ComputePassScopes& pass);
        void BuildReblur(ReblurStage stage, ComputePassScopes& pass);
    };

    void NRDSystem::State::BuildClear(ClearType type, ComputePassScopes& pass)
    {
        const auto& desc = *nrd::GetInstanceDesc(*m_instance);
        const ShaderInputBinding outputBinding{ desc.resourcesSpaceIndex, kClearOutput };
        const uint32_t pipeline = m_clearPipelines[ClearIndex(type)];
        for (const auto& dispatch : m_dispatches)
        {
            if (dispatch.pipelineIndex != pipeline)
            {
                continue;
            }
            auto scope = pass.Scope();
            scope.WriteImage(*m_textures->FindAttachment(dispatch.resources[0])).Bind(outputBinding);
            for (const auto& field : kClearFields)
            {
                scope.Constant(field.m_name,
                    eastl::span<const uint8_t>(dispatch.constantBufferData + field.m_offset, kClearFieldBytes));
            }
            scope.Dispatch(RHI::DispatchGroupCount{ dispatch.gridWidth, dispatch.gridHeight, 1 });
            scope.Close();
        }
    }

    void NRDSystem::State::BuildReblur(ReblurStage stage, ComputePassScopes& pass)
    {
        if (m_dispatches.empty())
        {
            return;
        }
        if (stage == ReblurStage::ClassifyTiles)
        {
            // Earlier Clear scopes resolve these names when the graph links attachments.
            m_textures->Declare(pass);
        }

        const auto& desc = *nrd::GetInstanceDesc(*m_instance);
        const uint32_t pipeline = m_reblurPipelines[StageIndex(stage)];
        const auto& constantInputs =
            m_layout->FindSpaceGroupBySpaceId(desc.constantBufferAndSamplersSpaceIndex)->m_shaderInputs;
        for (const auto& dispatch : m_dispatches)
        {
            if (dispatch.pipelineIndex != pipeline)
            {
                continue;
            }

            auto scope = pass.Scope();
            uint32_t srvIndex = 0;
            uint32_t uavIndex = 0;
            for (uint32_t i = 0; i < dispatch.resourcesNum; ++i)
            {
                const auto& resource = dispatch.resources[i];
                const auto& attachment = *m_textures->FindAttachment(resource);
                if (resource.descriptorType == nrd::DescriptorType::TEXTURE)
                {
                    const ShaderInputBinding binding{ desc.resourcesSpaceIndex, kTextures, srvIndex++ };
                    scope.ReadImage(attachment).Bind(binding);
                }
                else
                {
                    const ShaderInputBinding binding{ desc.resourcesSpaceIndex, kStorageTextures, uavIndex++ };
                    if (resource.type == nrd::ResourceType::IN_MV)
                    {
                        scope.ReadWriteImage(attachment).Bind(binding);
                    }
                    else
                    {
                        scope.WriteImage(attachment).Bind(binding);
                    }
                }
            }

            for (const auto& input : constantInputs)
            {
                if (input.m_type == RHI::ShaderInputType::Constant)
                {
                    const auto& field = m_layout->GetConstantDescriptor(input.m_index);
                    scope.Constant(field.m_name, eastl::span<const uint8_t>(
                        dispatch.constantBufferData + field.m_constantByteOffset, field.m_constantByteCount));
                }
            }

            scope.Dispatch(RHI::DispatchGroupCount{ dispatch.gridWidth, dispatch.gridHeight, 1 });
            scope.Close();
        }
    }

    void NRDSystem::Init(RHI::Factory& factory, RHI::Device& device)
    {
        if (m_state)
        {
            return;
        }

        const auto& library = *nrd::GetLibraryDesc();
        if (library.versionMajor != 4 || library.versionMinor != 18 || library.versionBuild != 0)
        {
            LOG_ERROR("[NRDSystem] Expected NRD 4.18.0 for the REBLUR constant layout, got {}.{}.{}.",
                static_cast<unsigned>(library.versionMajor), static_cast<unsigned>(library.versionMinor),
                static_cast<unsigned>(library.versionBuild));
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
        state->m_reblurPipelines.fill(kInvalidPipeline);
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
            else
            {
                const eastl::string_view identifier = pipeline.shaderIdentifier;
                const auto shader = identifier.substr(0, identifier.find('|'));
                const auto stage = eastl::find(kReblurShaders.begin(), kReblurShaders.end(), shader);
                if (stage != kReblurShaders.end()
                    && (shader != kReblurShaders[StageIndex(ReblurStage::PostBlur)]
                        || identifier.find("|TEMPORAL_STABILIZATION=1") != eastl::string_view::npos))
                {
                    state->m_reblurPipelines[stage - kReblurShaders.begin()] = i;
                }
            }
            state->m_pipelines.push_back(eastl::move(pso));
        }
        if (eastl::find(state->m_clearPipelines.begin(), state->m_clearPipelines.end(), kInvalidPipeline)
            != state->m_clearPipelines.end())
        {
            LOG_ERROR("[NRDSystem] The SDK is missing a Clear shader variant.");
            return;
        }
        for (size_t stage = 0; stage < kReblurStageCount; ++stage)
        {
            if (state->m_reblurPipelines[stage] == kInvalidPipeline)
            {
                LOG_ERROR("[NRDSystem] The SDK is missing the pipeline for {}.", kReblurShaders[stage].data());
                return;
            }
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

        const auto registerStage = [this](auto builder, ReblurStage stage)
        {
            const auto pipeline = m_state->m_reblurPipelines[StageIndex(stage)];
            builder.Queue(kQueue)
                .ImportPipeline(m_state->m_pipelines[pipeline], PassPipelineLayout{ m_state->m_layout, {} })
                .Build([this, stage](ComputePassScopes& pass) { m_state->BuildReblur(stage, pass); })
                .Finalize();
        };
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurClassifyTiles"), ReblurStage::ClassifyTiles);
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurPrePass"), ReblurStage::PrePass);
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurTemporalAccumulation"), ReblurStage::TemporalAccumulation);
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurHistoryFix"), ReblurStage::HistoryFix);
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurBlur"), ReblurStage::Blur);
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurPostBlur"), ReblurStage::PostBlur);
        registerStage(SPARK_COMPUTE_PASS(passes, "NRDReblurTemporalStabilization"), ReblurStage::TemporalStabilization);
    }

    void NRDSystem::PrepareFrame(RHI::RHIContext& rhiContext, const FrameTime& time)
    {
        if (!m_state)
        {
            return;
        }
        auto& state = *m_state;
        rhiContext.Clear<NRDViewOutput>();
        state.m_dispatches = {};

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
                settings.resourceSize, settings.rectSize, settings.cameraJitter))
        {
            return;
        }

        const bool resize = !state.m_textures
            || state.m_textures->m_width != settings.resourceSize[0] || state.m_textures->m_height != settings.resourceSize[1];
        const bool restart = resize || state.m_lastView != mainView || !previous.m_valid
            || time.m_frameNumber - state.m_lastFrameNumber != 1u;
        if (!SetViewSettings(restart ? view.m_bufferSize : previous.m_bufferSize,
                restart ? view.m_rect : previous.m_rect, restart ? view.m_jitter : previous.m_jitter,
                settings.resourceSizePrev, settings.rectSizePrev, settings.cameraJitterPrev))
        {
            return;
        }

        CopyMatrix(settings.worldToViewMatrix, view.m_worldToView);
        CopyMatrix(settings.viewToClipMatrix, view.m_viewToClip);
        CopyMatrix(settings.worldToViewMatrixPrev, restart ? view.m_worldToView : previous.m_worldToView);
        CopyMatrix(settings.viewToClipMatrixPrev, restart ? view.m_viewToClip : previous.m_viewToClip);
        settings.timeDeltaBetweenFrames = time.m_realDeltaTime * 1000.0f;
        settings.frameIndex = static_cast<uint32_t>(time.m_frameNumber);
        settings.isHistoryConfidenceAvailable = true;
        settings.accumulationMode = restart ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;

        auto& instance = *state.m_instance;
        const auto& desc = *nrd::GetInstanceDesc(instance);
        UniquePtr<FrameTextures> replacement;
        if (resize)
        {
            replacement = MakeUnique<FrameTextures>();
            if (!replacement->DescribeTextures(desc, settings.resourceSize[0], settings.resourceSize[1]))
            {
                return;
            }
        }
        auto result = nrd::SetCommonSettings(instance, settings);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDSystem] SetCommonSettings failed ({}).", static_cast<unsigned>(result));
            return;
        }

        const float deltaTime = time.m_realDeltaTime;
        const bool validDeltaTime = deltaTime > 0.0f && deltaTime <= (eastl::numeric_limits<float>::max)();
        const float frameTime = validDeltaTime
            ? eastl::clamp(deltaTime, 0.001f, 0.25f) : state.m_smoothedFrameTime;
        const float smoothingWeight = frameTime / (kFrameTimeSmoothingSeconds + frameTime);
        const float smoothedFrameTime = restart ? frameTime
            : state.m_smoothedFrameTime + (frameTime - state.m_smoothedFrameTime) * smoothingWeight;

        auto reblurSettings = state.m_reblurSettings;
        reblurSettings.maxAccumulatedFrameNum = eastl::clamp(
            nrd::GetMaxAccumulatedFrameNum(kHistoryTimeSeconds, 1.0f / smoothedFrameTime),
            uint32_t{ 1 }, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
        const uint32_t fastHistoryFrames = reblurSettings.maxAccumulatedFrameNum / 5;
        reblurSettings.maxFastAccumulatedFrameNum = fastHistoryFrames;
        reblurSettings.historyFixFrameNum = eastl::min(reblurSettings.historyFixFrameNum,
            fastHistoryFrames > 0 ? fastHistoryFrames - 1 : 0u);
        reblurSettings.maxStabilizedFrameNum = reblurSettings.maxAccumulatedFrameNum;
        result = nrd::SetDenoiserSettings(instance, kDenoiserId, &reblurSettings);
        if (result != nrd::Result::SUCCESS)
        {
            LOG_ERROR("[NRDSystem] SetDenoiserSettings failed ({}).", static_cast<unsigned>(result));
            return;
        }

        const nrd::DispatchDesc* dispatches = nullptr;
        uint32_t count = 0;
        result = nrd::GetComputeDispatches(instance, &kDenoiserId, 1, dispatches, count);
        if (result != nrd::Result::SUCCESS || !dispatches || count == 0)
        {
            LOG_ERROR("[NRDSystem] Could not prepare the REBLUR dispatches ({}).", static_cast<unsigned>(result));
            return;
        }

        if (replacement)
        {
            if (!replacement->AllocatePermanentTextures(rhiContext, state.m_factory, *state.m_permanentPool))
            {
                return;
            }
            if (state.m_textures)
            {
                state.m_textures->ReleasePermanentTextures(rhiContext);
            }
            state.m_textures = eastl::move(replacement);
        }
        if (restart)
        {
            LOG_INFO("[NRDSystem] Prepared {} NRD dispatches for MainView {}x{} after restart.",
                count, settings.rectSize[0], settings.rectSize[1]);
        }

        state.m_dispatches = eastl::span<const nrd::DispatchDesc>(dispatches, count);
        state.m_lastView = mainView;
        state.m_lastFrameNumber = time.m_frameNumber;
        state.m_smoothedFrameTime = smoothedFrameTime;
        rhiContext.Add<NRDViewOutput>(mainView, NRDViewOutput{
            { settings.rectSize[0], settings.rectSize[1] }, settings.denoisingRange / settings.viewZScale });
    }

    void NRDSystem::Shutdown(RHI::RHIContext& rhiContext)
    {
        if (m_state && m_state->m_textures)
        {
            m_state->m_textures->ReleasePermanentTextures(rhiContext);
            rhiContext.Clear<NRDViewOutput>();
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
