#pragma once

#include <cstdint>

#include <RHI/Format.h>

#include "View.h"

namespace Spark::Render
{
    //! One shadow atlas split into a fixed power-of-two grid. Power-of-two, and the atlas a
    //! multiple of it, so a tile's normalized rect times the atlas extent lands exactly on
    //! integers — no half-texel drift between the viewport and the sampled UVs.
    //!
    //! Tile count is the shadow budget: it bounds the cost regardless of how many lights the
    //! scene has. Keep it in step with ViewHandleList's inline capacity (PassCapabilities.h).
    inline constexpr uint32_t kShadowAtlasResolution = 4096;
    inline constexpr uint32_t kShadowTileGrid        = 4;   // 4x4
    inline constexpr uint32_t kShadowTileCount       = kShadowTileGrid * kShadowTileGrid;
    inline constexpr uint32_t kShadowTileResolution  = kShadowAtlasResolution / kShadowTileGrid;

    //! One resource, two views: D32 DSV for ShadowPass, R32_FLOAT SRV for LightingPass.
    inline constexpr RHI::Format kShadowAtlasFormat = RHI::Format::D32_FLOAT;

    //! No tile free. Distinct from slot 0, which is a perfectly good tile.
    inline constexpr uint32_t kInvalidShadowSlot = ~0u;

    //! The one atlas image, owned by ShadowViewSystem. How ShadowPass locates it.
    struct ShadowAtlasTag {};

    //! Texels held back on each side of a tile, so PCF taps at a tile's edge land here rather
    //! than in the neighbour. Must cover the kernel's reach: a 3x3 at one texel spacing plus
    //! the sampler's own 2x2 footprint reaches 1.5 texels.
    inline constexpr uint32_t kShadowTileBorderTexels = 2;

    //! Texels a tile's viewport actually spans. This — not the tile resolution — is what
    //! NDC [-1,1] maps onto, so it is the divisor for a texel's world size.
    inline constexpr uint32_t kShadowTileUsableTexels =
        kShadowTileResolution - 2 * kShadowTileBorderTexels;

    //! A tile's INSET rect. Viewport, scissor, the tile remap baked into the shadow matrix
    //! and the sampling clamp all derive from this one value, so a border that reached only
    //! some of them — which shifts every sampled UV by its width — cannot happen.
    inline ViewRect ShadowTileRect(uint32_t slot)
    {
        const uint32_t gx = slot % kShadowTileGrid;
        const uint32_t gy = slot / kShadowTileGrid;

        constexpr float tile   = 1.0f / static_cast<float>(kShadowTileGrid);
        constexpr float border = static_cast<float>(kShadowTileBorderTexels)
                               / static_cast<float>(kShadowAtlasResolution);

        // Field order is (minX, maxX, minY, maxY) — it mirrors Viewport::GetScaled's
        // parameters, not the usual (min, min, max, max) corner pairing.
        return ViewRect{
            static_cast<float>(gx)     * tile + border,
            static_cast<float>(gx + 1) * tile - border,
            static_cast<float>(gy)     * tile + border,
            static_cast<float>(gy + 1) * tile - border,
        };
    }

    //! Clip -> atlas UV for one tile: the NDC-to-UV flip and the tile's scale/offset in one
    //! matrix. Left-multiply a view's worldToClip to get world -> atlas UV.
    //!
    //! Only Y is flipped, and only here. The rect's minY/maxY are already in the downward-Y
    //! space viewport and texture V share, so the tile step uses them as they are.
    //! Z is left alone — LH_ZO clip depth is the 0..1 the comparison wants.
    inline Math::Matrix4X4 MakeShadowUVRemap(const ViewRect& rect)
    {
        const float sx = rect.m_maxX - rect.m_minX;
        const float sy = rect.m_maxY - rect.m_minY;

        Math::Matrix4X4 m = Math::Matrix4X4Const::IDENTITY;
        m[0][0] =  0.5f * sx;   m[3][0] = 0.5f * sx + rect.m_minX;
        m[1][1] = -0.5f * sy;   m[3][1] = 0.5f * sy + rect.m_minY;
        return m;
    }
}
