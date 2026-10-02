#include "HZBPass.h"

#include <EASTL/array.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <View/View.h>
#include <View/ViewTags.h>

#include <Feature/SceneTextures/SceneTextures.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    void HZBPass::SetUp(PassContext& ctx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/HZB/HZB.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[HZBPass] Failed to load shader HZB.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        SPARK_COMPUTE_PASS(ctx, "HZBPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Build([](ComputePassScopes& p)
            {
                using namespace SceneTextures;

                auto& rhiContext = *RHI::RHIExecuteContext::Current();

                // The chain is laid over the whole scene depth, so the main view must cover
                // all of it.
                for (auto [entity, view] : rhiContext.GetView<MainViewTag, View>().each())
                {
                    ASSERT(view.m_rect.m_minX == 0.0f && view.m_rect.m_maxX == 1.0f
                        && view.m_rect.m_minY == 0.0f && view.m_rect.m_maxY == 1.0f,
                        "[HZBPass] The main view does not cover its render target.");
                }

                const Math::Vector2Int renderSize = p.GetRenderSize();
                const uint32_t         mipCount   = HZB::MipCount(renderSize);
                const Math::Vector2Int mip0Size   = HZB::MipSize(renderSize, 0);

                RHI::ImageDescriptor desc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::ShaderReadWrite,
                    static_cast<uint32_t>(mip0Size.x), static_cast<uint32_t>(mip0Size.y),
                    HZB::kFormat);
                desc.m_mipLevels = static_cast<uint16_t>(mipCount);
                p.CreateImage(HZB::ClosestName(), desc);
                p.CreateImage(HZB::FurthestName(), desc);

                const RHI::InputName closestInput("closestInputIndex");
                const RHI::InputName furthestInput("furthestInputIndex");

                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    const Math::Vector2Int size = HZB::MipSize(renderSize, mip);
                    Math::Vector2Int       inputSize;

                    auto s = p.Scope();
                    if (mip == 0)
                    {
                        // Both chains start from the scene depth: one access per index.
                        const RHI::AttachmentId sceneDepth("SceneDepth");
                        inputSize = renderSize;
                        s.Read(sceneDepth).Format(RHI::Format::R32_FLOAT).BindIndex(closestInput);
                        s.Read(sceneDepth).Format(RHI::Format::R32_FLOAT).BindIndex(furthestInput);
                    }
                    else
                    {
                        const uint16_t above = static_cast<uint16_t>(mip - 1);
                        const RHI::ImageViewDescriptor inputView =
                            RHI::ImageViewDescriptor::Create(RHI::Format::Unknown, above, above);
                        inputSize = HZB::MipSize(renderSize, mip - 1);
                        s.Read(HZB::ClosestName()).View(inputView).BindIndex(closestInput);
                        s.Read(HZB::FurthestName()).View(inputView).BindIndex(furthestInput);
                    }

                    const RHI::ImageViewDescriptor outputView = RHI::ImageViewDescriptor::Create(
                        RHI::Format::Unknown, static_cast<uint16_t>(mip), static_cast<uint16_t>(mip));
                    s.Write(HZB::ClosestName()).View(outputView).BindIndex(RHI::InputName("closestOutputIndex"));
                    s.Write(HZB::FurthestName()).View(outputView).BindIndex(RHI::InputName("furthestOutputIndex"));

                    s.Constant(RHI::InputName("inputSize"), eastl::array<uint32_t, 2>{
                        static_cast<uint32_t>(inputSize.x), static_cast<uint32_t>(inputSize.y) });
                    s.Constant(RHI::InputName("outputSize"), eastl::array<uint32_t, 2>{
                        static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y) });
                    s.Dispatch(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                    s.Close();
                }
            })
            .Finalize()
        ;
    }
}
