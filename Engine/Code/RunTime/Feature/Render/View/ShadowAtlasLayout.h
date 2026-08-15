#pragma once

#include <cstdint>

#include <Memory/QuadTreeAllocator.h>
#include <RHI/Format.h>

#include "View.h"

namespace Spark::Render
{
    //! One shadow atlas, cut into power-of-two square blocks. Power-of-two, and the atlas a
    //! multiple of them, so a block's normalized rect times the atlas extent lands exactly on
    //! integers — no half-texel drift between the viewport and the sampled UVs.
    inline constexpr uint32_t kShadowAtlasResolution = 4096;

    //! Finest block the atlas can be cut into: 4096 >> 4 = 256 texels. It bounds the tree,
    //! not the policy — which levels a light may actually ask for is decided elsewhere.
    inline constexpr uint32_t kShadowAtlasMaxLevel = 4;

    using ShadowAtlasAllocator = QuadTreeAllocator<kShadowAtlasMaxLevel>;

    //! The one level handed out today, 4096 >> 2 = 1024 texels. Choosing it per light by
    //! screen coverage is the resolution ladder, still to come.
    inline constexpr uint32_t kShadowTileLevel      = 2;
    inline constexpr uint32_t kShadowTileGrid       = 1u << kShadowTileLevel;
    inline constexpr uint32_t kShadowTileResolution = kShadowAtlasResolution >> kShadowTileLevel;

    //! The shadow budget while every light takes the same level: it bounds the cost
    //! regardless of how many lights the scene has. Once levels vary this stops being a
    //! count and becomes atlas area. Keep it in step with ViewHandleList's inline capacity
    //! (PassCapabilities.h).
    inline constexpr uint32_t kShadowTileCount = kShadowTileGrid * kShadowTileGrid;

    //! Rows in g_ShadowViews. A DIFFERENT quantity from the tile count, which it merely
    //! happens to equal today: a row is a matrix plus a rect, an atlas tile is space to
    //! rasterize into. A resolution ladder varies tile size without touching row size, and
    //! a point light will take six rows for however many tiles its faces end up in.
    inline constexpr uint32_t kShadowViewCapacity = kShadowTileCount;

    //! One resource, two views: D32 DSV for ShadowPass, R32_FLOAT SRV for LightingPass.
    inline constexpr RHI::Format kShadowAtlasFormat = RHI::Format::D32_FLOAT;

    //! Nothing free — from either allocator. Distinct from 0, a perfectly good tile and row.
    inline constexpr uint32_t kInvalidShadowSlot = ~0u;

    //! The one atlas image, owned by ShadowViewSystem. How ShadowPass locates it.
    struct ShadowAtlasTag {};

    //! One texel held back on each side of a tile. It absorbs the bilinear footprint of a tap
    //! sitting exactly on the tile's edge, and nothing else: the PCF kernel clamps its own
    //! taps into the tile (Lib/Lights.hlsli), so this does NOT scale with the kernel radius.
    inline constexpr uint32_t kShadowTileBorderTexels = 1;

    //! Texels a tile's viewport actually spans. This — not the tile resolution — is what
    //! NDC [-1,1] maps onto, so it is the divisor for a texel's world size.
    inline constexpr uint32_t kShadowTileUsableTexels = kShadowTileResolution - 2 * kShadowTileBorderTexels;

    //! An allocated block's INSET rect. Viewport, scissor, the tile remap baked into the
    //! shadow matrix and the sampling clamp all derive from this one value, so a border that
    //! reached only some of them — which shifts every sampled UV by its width — cannot happen.
    //!
    //! This is the whole of the shadow layer's knowledge of what a block MEANS. The allocator
    //! deals in blocks and levels and has no opinion about borders or texels.
    inline ViewRect ShadowTileRect(uint32_t node)
    {
        const ShadowAtlasAllocator::Block block = ShadowAtlasAllocator::Decode(node);
        const uint32_t gx = block.m_x;
        const uint32_t gy = block.m_y;

        // Blocks of a coarser level are wider, so the span is no longer a constant. The
        // border is: it is a fixed count of atlas texels whatever the block's size.
        const float     span   = 1.0f / static_cast<float>(1u << block.m_level);
        constexpr float border = static_cast<float>(kShadowTileBorderTexels)
                               / static_cast<float>(kShadowAtlasResolution);

        // Field order is (minX, maxX, minY, maxY) — it mirrors Viewport::GetScaled's
        // parameters, not the usual (min, min, max, max) corner pairing.
        return ViewRect{
            static_cast<float>(gx)     * span + border,
            static_cast<float>(gx + 1) * span - border,
            static_cast<float>(gy)     * span + border,
            static_cast<float>(gy + 1) * span - border,
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
