#pragma once

#include <Base.h>
#include <Math/Vector2.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Context/RHIContext.h>

//! The resources the post-process passes hand one another: their names and shapes, defined
//! once here so no pass reaches into another to learn them. Each is written by one pass and
//! read by the ones after it.
namespace Spark::Render::PostProcess
{
    //! Written by TemporalAAPass.
    const RHI::AttachmentId& TemporalAAName();

    //! The finished HDR scene color: TemporalAA while the main view runs temporal AA,
    //! SceneColor otherwise.
    const RHI::AttachmentId& SceneColorName(RHI::RHIContext& ctx);

    //! The scene color halved level by level, written by SceneDownsamplePass. Level 1 is half
    //! the render size.
    namespace SceneDownsampleChain
    {
        constexpr uint32_t kLevelCountMax = 8;

        //! The smallest level lands near 16-32 pixels, so the chain spans the same fraction of
        //! the screen at any resolution. At least 2, at most kLevelCountMax.
        uint32_t LevelCount(const Math::Vector2Int& renderSize);

        //! Rounds up, so an odd side keeps its last pixel column.
        Math::Vector2Int LevelSize(const Math::Vector2Int& renderSize, uint32_t level);

        const RHI::AttachmentId& LevelName(uint32_t level);
    }

    //! The glow, written by BloomPass at the size of the chain's level 1.
    const RHI::AttachmentId& BloomName();

    //! The glow is the sum of every chain level, each carrying the whole image's energy; this
    //! scales it back to one.
    float BloomScale(const Math::Vector2Int& renderSize);
}
