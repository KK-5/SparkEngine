#pragma once

#include <Base.h>
#include <Math/Vector2.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Context/RHIContext.h>
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

        //! Whether a pass reads the chains this frame; HZBPass builds them only then. A new
        //! reader adds its own condition here.
        bool HasReader(RHI::RHIContext& ctx);
    }

    //! Screen-space ambient occlusion, written by AmbientOcclusionPass at the render size: how
    //! much of the hemisphere over each pixel is open, 1 where nothing occludes it. It applies
    //! to indirect light only, and its intensity is already in it.
    //!
    //! Not every frame has it. A pass that reads it asks FindView first and, when there is
    //! none, neither declares the read nor samples the texture.
    namespace AmbientOcclusion
    {
        constexpr RHI::Format kFormat = RHI::Format::R32_FLOAT;

        //! The effect samples a depth chain of this many mips, so the target must have them.
        constexpr uint32_t kDepthMipCount = 5;

        const RHI::AttachmentId& Name();

        //! The main view the signal is produced for this frame, or NullHandle: the view has
        //! no ViewAmbientOcclusion, no g_Views slot yet, or a target too small for the chain.
        RHI::RHIHandle FindView(RHI::RHIContext& ctx);
    }

    //! Screen-space reflections, written by ScreenSpaceReflectionsPass at the render size. rgb
    //! is the radiance arriving along each pixel's mirror direction, scene-linear (no
    //! pre-exposure); a is how far it replaces the environment's reflection: 0 where the ray
    //! found nothing, the surface is too rough, or the hit cannot be trusted, and the view's
    //! intensity at most.
    //!
    //! Not every frame has them. A pass that reads them asks FindView first, as with
    //! AmbientOcclusion.
    namespace ScreenSpaceReflections
    {
        constexpr RHI::Format kFormat = RHI::Format::R16G16B16A16_FLOAT;

        const RHI::AttachmentId& Name();

        //! The main view the signal is produced for this frame, or NullHandle: the view has
        //! no ViewScreenSpaceReflection, no g_Views slot yet, or no temporal AA, whose last
        //! output is where a hit's color comes from.
        RHI::RHIHandle FindView(RHI::RHIContext& ctx);
    }
}
