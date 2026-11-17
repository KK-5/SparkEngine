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
    //! Direct lighting before composition: one light sample + one pure GGX sample
    //! per pixel, specular MIS, no history or pre-exposure. RGB is divided by
    //! DirectLightingSignal.hlsli's material factors (floor 0.02).
    //! Diffuse alpha is receiver-to-light distance, retained for occluded samples
    //! and lights with shadows disabled; invalid/zero-response light samples use 0.
    //! Specular alpha is the nearest scene triangle or visible, nonzero emitting
    //! rectangle along the GGX direction. Ordinary surfaces provide distance only.
    //! Non-shadowing rectangles behind geometry contribute RGB without replacing
    //! its nearer distance. Black specular responses retain scene distance only.
    //! Rejected GGX directions use 0; miss/infinite distances are capped at 65504.
    //! Distances use the original receiver, not corrected shadow origins, and are
    //! neither averaged nor divided by PDFs/material factors or weighted by MIS.
    //! The guides use signed world normals + effective perceptual roughness
    //! (minimum 0.045, matching the BRDF/GGX sampler), positive linear
    //! viewZ (1e30 for background), and unjittered previousUV-currentUV motion.
    //! These are engine signals; an NRD adapter must use the SDK's packing helpers
    //! and matching encoding settings. REBLUR also needs hit-distance normalization.
    namespace DirectLighting
    {
        // Preserve high-variance samples before a denoiser's explicit range handling.
        constexpr RHI::Format kRadianceFormat = RHI::Format::R32G32B32A32_FLOAT;
        constexpr RHI::Format kNormalRoughnessFormat = RHI::Format::R16G16B16A16_FLOAT;
        constexpr RHI::Format kViewZFormat = RHI::Format::R32_FLOAT;
        constexpr RHI::Format kMotionFormat = RHI::Format::R16G16_FLOAT;

        const RHI::AttachmentId& DiffuseName();
        const RHI::AttachmentId& SpecularName();
        const RHI::AttachmentId& NormalRoughnessName();
        const RHI::AttachmentId& ViewZName();
        const RHI::AttachmentId& MotionName();
    }

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
        //! no ViewAmbientOcclusion, no g_Views row this frame, or a target too small for the chain.
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
        //! no ViewScreenSpaceReflection, no g_Views row this frame, or no temporal AA, whose last
        //! output is where a hit's color comes from.
        RHI::RHIHandle FindView(RHI::RHIContext& ctx);
    }
}
