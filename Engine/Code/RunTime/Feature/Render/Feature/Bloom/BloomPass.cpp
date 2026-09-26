#include "BloomPass.h"

#include <EASTL/array.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <View/MainView.h>
#include <View/ViewComponents.h>

#include <Feature/PostProcess/PostProcessResources.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        using namespace PostProcess::SceneDownsampleChain;

        //! The glow accumulated down to `level`; level 1 is the output. The chain's smallest
        //! level has none: the glow starts from that level itself.
        const RHI::AttachmentId& UpName(uint32_t level)
        {
            static const eastl::array<RHI::AttachmentId, kLevelCountMax - 1> s_names = {
                PostProcess::BloomName(),  RHI::AttachmentId("BloomUp2"), RHI::AttachmentId("BloomUp3"),
                RHI::AttachmentId("BloomUp4"), RHI::AttachmentId("BloomUp5"), RHI::AttachmentId("BloomUp6"),
                RHI::AttachmentId("BloomUp7"),
            };
            ASSERT(level >= 1 && level < kLevelCountMax, "[BloomPass] Level {} is out of range.", level);
            return s_names[level - 1];
        }
    }

    void BloomPass::SetUp(PassContext& ctx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/Bloom/BloomUpsample.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[BloomPass] Failed to load shader BloomUpsample.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(ctx, "BloomPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([](ComputePassScopes& p)
            {
                if (FindMainViewComponent<ViewBloom>(*RHI::RHIExecuteContext::Current()) == nullptr)
                {
                    return;
                }

                const Math::Vector2Int renderSize = p.GetRenderSize();
                const uint32_t levelCount = LevelCount(renderSize);

                for (uint32_t level = levelCount - 1; level >= 1; --level)
                {
                    const Math::Vector2Int size = LevelSize(renderSize, level);
                    p.CreateImage(UpName(level), RHI::ImageDescriptor::Create2D(
                        RHI::ImageBindFlags::ShaderReadWrite,
                        static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y),
                        RHI::Format::R16G16B16A16_FLOAT));
                }

                const RHI::SamplerState linearClamp = RHI::SamplerState::Create(
                    RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp);

                // Smallest first: each Scope reads the glow the one before it wrote.
                for (uint32_t level = levelCount - 1; level >= 1; --level)
                {
                    const RHI::AttachmentId& low = level == levelCount - 1
                        ? LevelName(levelCount) : UpName(level + 1);
                    const Math::Vector2Int lowSize = LevelSize(renderSize, level + 1);
                    const Math::Vector2Int size    = LevelSize(renderSize, level);

                    auto s = p.Scope();
                    s.Read(LevelName(level)).BindIndex(RHI::InputName("currentIndex"));
                    s.Read(low).BindIndex(RHI::InputName("lowIndex"));
                    s.Write(UpName(level)).BindIndex(RHI::InputName("outputIndex"));
                    s.Constant(RHI::InputName("lowInvSize"), Math::Vector2(
                        1.0f / static_cast<float>(lowSize.x), 1.0f / static_cast<float>(lowSize.y)));
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
