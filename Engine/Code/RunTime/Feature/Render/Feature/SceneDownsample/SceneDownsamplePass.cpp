#include "SceneDownsamplePass.h"

#include <EASTL/array.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <View/View.h>
#include <View/MainView.h>
#include <View/ViewComponents.h>

#include <Feature/PostProcess/PostProcessResources.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! Whether anything reads the chain this frame. Bloom is its only reader today.
        bool IsNeeded(RHI::RHIContext& ctx)
        {
            return FindMainViewComponent<ViewBloom>(ctx) != nullptr;
        }
    }

    void SceneDownsamplePass::SetUp(PassContext& ctx)
    {
        using namespace PostProcess::SceneDownsampleChain;

        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/SceneDownsample/SceneDownsample.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[SceneDownsamplePass] Failed to load shader SceneDownsample.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(ctx, "SceneDownsamplePass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([](ComputePassScopes& p)
            {
                auto& rhiContext = *RHI::RHIExecuteContext::Current();
                if (!IsNeeded(rhiContext))
                {
                    return;
                }

                // Level 1 is sampled over the whole scene color, so the main view must cover
                // all of it. A view that does not needs its rect passed in (PostProcessPlan D9).
                for (auto [entity, view] : rhiContext.GetView<MainViewTag, View>().each())
                {
                    ASSERT(view.m_rect.m_minX == 0.0f && view.m_rect.m_maxX == 1.0f
                        && view.m_rect.m_minY == 0.0f && view.m_rect.m_maxY == 1.0f,
                        "[SceneDownsamplePass] The main view does not cover its render target.");
                }

                const Math::Vector2Int renderSize = p.GetRenderSize();
                const uint32_t levelCount = LevelCount(renderSize);

                for (uint32_t level = 1; level <= levelCount; ++level)
                {
                    const Math::Vector2Int size = LevelSize(renderSize, level);
                    p.CreateImage(LevelName(level), RHI::ImageDescriptor::Create2D(
                        RHI::ImageBindFlags::ShaderReadWrite,
                        static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y),
                        RHI::Format::R16G16B16A16_FLOAT));
                }

                const RHI::SamplerState linearClamp = RHI::SamplerState::Create(
                    RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp);

                for (uint32_t level = 1; level <= levelCount; ++level)
                {
                    const bool first = level == 1;
                    const RHI::AttachmentId& input = first
                        ? PostProcess::SceneColorName(rhiContext) : LevelName(level - 1);
                    const Math::Vector2Int inputSize = first ? renderSize : LevelSize(renderSize, level - 1);
                    const Math::Vector2Int size      = LevelSize(renderSize, level);

                    auto s = p.Scope();
                    s.Read(input).BindIndex(RHI::InputName("inputIndex"));
                    s.Write(LevelName(level)).BindIndex(RHI::InputName("outputIndex"));
                    s.Constant(RHI::InputName("inputInvSize"), Math::Vector2(
                        1.0f / static_cast<float>(inputSize.x), 1.0f / static_cast<float>(inputSize.y)));
                    s.Constant(RHI::InputName("outputSize"), eastl::array<uint32_t, 2>{
                        static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y) });
                    s.Sampler(RHI::InputName("g_LinearSampler"), linearClamp);
                    s.Dispatch(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                }
            })
            .Finalize()
        ;
    }
}
