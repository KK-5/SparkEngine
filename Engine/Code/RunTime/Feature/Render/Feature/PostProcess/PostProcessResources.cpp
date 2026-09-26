#include "PostProcessResources.h"

#include <EASTL/array.h>

#include <View/MainView.h>
#include <View/ViewComponents.h>

namespace Spark::Render::PostProcess
{
    const RHI::AttachmentId& TemporalAAName()
    {
        static const RHI::AttachmentId s_name("TemporalAA");
        return s_name;
    }

    const RHI::AttachmentId& SceneColorName(RHI::RHIContext& ctx)
    {
        static const RHI::AttachmentId s_sceneColor("SceneColor");
        return FindMainViewComponent<ViewTemporalAA>(ctx) != nullptr ? TemporalAAName() : s_sceneColor;
    }

    namespace SceneDownsampleChain
    {
        uint32_t LevelCount(const Math::Vector2Int& renderSize)
        {
            const uint32_t minSide = static_cast<uint32_t>(eastl::min(renderSize.x, renderSize.y));
            int32_t log2 = 0;
            while ((minSide >> (log2 + 1)) != 0)
            {
                ++log2;
            }
            return static_cast<uint32_t>(eastl::clamp(log2 - 4, 2, static_cast<int32_t>(kLevelCountMax)));
        }

        Math::Vector2Int LevelSize(const Math::Vector2Int& renderSize, uint32_t level)
        {
            Math::Vector2Int size = renderSize;
            for (uint32_t i = 0; i < level; ++i)
            {
                size.x = eastl::max((size.x + 1) / 2, 1);
                size.y = eastl::max((size.y + 1) / 2, 1);
            }
            return size;
        }

        const RHI::AttachmentId& LevelName(uint32_t level)
        {
            static const eastl::array<RHI::AttachmentId, kLevelCountMax> s_names = {
                RHI::AttachmentId("SceneDownsample1"), RHI::AttachmentId("SceneDownsample2"),
                RHI::AttachmentId("SceneDownsample3"), RHI::AttachmentId("SceneDownsample4"),
                RHI::AttachmentId("SceneDownsample5"), RHI::AttachmentId("SceneDownsample6"),
                RHI::AttachmentId("SceneDownsample7"), RHI::AttachmentId("SceneDownsample8"),
            };
            ASSERT(level >= 1 && level <= kLevelCountMax, "[PostProcess] Downsample level {} is out of range.", level);
            return s_names[level - 1];
        }
    }

    const RHI::AttachmentId& BloomName()
    {
        static const RHI::AttachmentId s_name("Bloom");
        return s_name;
    }

    float BloomScale(const Math::Vector2Int& renderSize)
    {
        return 1.0f / static_cast<float>(SceneDownsampleChain::LevelCount(renderSize));
    }
}
