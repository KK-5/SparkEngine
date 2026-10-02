#pragma once

#include <Base.h>
#include <Math/Vector2.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Format.h>

//! The scene's shared textures: their names and shapes, defined once here so no pass reaches
//! into another to learn them. Each is written by one pass and read by the ones after it.
namespace Spark::Render::SceneTextures
{
    //! The hierarchical Z-buffer, written by HZBPass: two mip chains over the scene depth, each
    //! texel the closest / the furthest depth of the pixels under it. Depth is reversed-Z, so
    //! closest is the largest value.
    //!
    //! A texel of mip 0 covers 2x2 pixels and each mip halves the one above exactly, so the
    //! sides are powers of two and the chain is wider than the screen: the screen takes the
    //! corner UvFactor of it, and texels past the screen repeat its edge.
    namespace HZB
    {
        constexpr RHI::Format kFormat = RHI::Format::R32_FLOAT;

        const RHI::AttachmentId& ClosestName();
        const RHI::AttachmentId& FurthestName();

        //! Down to 1x1.
        uint32_t MipCount(const Math::Vector2Int& renderSize);

        Math::Vector2Int MipSize(const Math::Vector2Int& renderSize, uint32_t mip);

        //! Screen UV times this is HZB UV.
        Math::Vector2 UvFactor(const Math::Vector2Int& renderSize);
    }
}
