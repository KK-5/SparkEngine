#pragma once

#include <cstdint>

#include <Memory/QuadTreeAllocator.h>
#include <RHI/Format.h>

#include "View.h"

namespace Spark::Render
{
    //! One shadow atlas, cut into power-of-two square tiles. Power-of-two, and the atlas a
    //! multiple of them, so a tile's normalized rect times the atlas extent lands exactly on
    //! integers — no half-texel drift between the viewport and the sampled UVs.
    //!
    //! "Tile" throughout this layer, "block" only inside QuadTreeAllocator: the allocator is
    //! a tree over a square and knows nothing of atlases, borders or texels.
    inline constexpr uint32_t kShadowAtlasResolution = 4096;

    //! Finest tile the atlas can be cut into: 4096 >> 4 = 256 texels. It bounds the tree, not
    //! the policy — which levels a light may actually ask for is decided just below.
    inline constexpr uint32_t kShadowAtlasMaxLevel = 4;

    //! The tree the atlas is cut by. ShadowAtlasAllocator owns the one instance; this name is
    //! spelled out here only for the handful of places that decode a tile id without holding
    //! the allocator.
    using ShadowTileTree = QuadTreeAllocator<kShadowAtlasMaxLevel>;

    //! The levels a light may be granted. Level 0 — the whole atlas to one light — is
    //! deliberately out of reach: no light is worth every other light's shadow. The finest
    //! level bounds how many shadow views can exist at once, and so how many passes a frame
    //! can be asked to render, which is why it stops short of the tree's depth.
    inline constexpr uint32_t kShadowCoarsestLevel = 1;   // 4096 >> 1 = 2048 texels
    inline constexpr uint32_t kShadowFinestLevel   = 3;   // 4096 >> 3 =  512 texels

    //! The budget, counted in tiles of the finest level — 4^3 = 64 of them cover the atlas.
    //! Area rather than a count of tiles, because a tile no longer has one size.
    inline constexpr uint32_t kShadowBudgetUnits = 1u << (2 * kShadowFinestLevel);

    //! What one tile of a level costs against that budget. Each level up quadruples it.
    inline constexpr uint32_t ShadowTileCost(uint32_t level)
    {
        return 1u << (2 * (kShadowFinestLevel - level));
    }

    inline constexpr uint32_t kShadowCubeFaceCount = 6;

    //! Rows in g_ShadowViews. A DIFFERENT quantity from the tiles: a row is a matrix plus a
    //! rect, a tile is space to rasterize into. A light reserves a row per face whether or
    //! not the face is granted a tile, so the bound is every tile going to a different light
    //! that culled all but one of its faces.
    //!
    //! Not the inline capacity of ViewHandleList (PassCapabilities.h), which spills to the
    //! heap rather than being wrong.
    inline constexpr uint32_t kShadowViewCapacity = kShadowBudgetUnits * kShadowCubeFaceCount;

    //! One resource, two views: D32 DSV for ShadowPass, R32_FLOAT SRV for LightingPass.
    inline constexpr RHI::Format kShadowAtlasFormat = RHI::Format::D32_FLOAT;

    //! Nothing free — from either allocator. Distinct from 0, a perfectly good tile and row.
    inline constexpr uint32_t kInvalidShadowSlot = ~0u;

    //! No level: the light holds no tile, so there is nothing to be hysteretic about, and
    //! nothing an allocation could be granted at.
    inline constexpr uint32_t kNoShadowLevel = ~0u;

    //! The one atlas image, owned by ShadowViewSystem. How ShadowPass locates it.
    struct ShadowAtlasTag {};

    //! One texel held back on each side of a tile. It absorbs the bilinear footprint of a tap
    //! sitting exactly on the tile's edge, and nothing else: the PCF kernel clamps its own
    //! taps into the tile (Lib/Lights.hlsli), so this does NOT scale with the kernel radius.
    //! A fixed count of texels whatever the tile's size.
    inline constexpr uint32_t kShadowTileBorderTexels = 1;

    //! Texels a tile's viewport actually spans. This — not the tile resolution — is what NDC
    //! [-1,1] maps onto, so it is the divisor for a texel's world size. Per level now that a
    //! tile's size is not fixed: anything that snaps to the texel grid has to ask the level it
    //! was actually granted, not a constant.
    inline constexpr uint32_t ShadowUsableTexels(uint32_t level)
    {
        return (kShadowAtlasResolution >> level) - 2 * kShadowTileBorderTexels;
    }

    //! A tile's INSET rect. Viewport, scissor, the tile remap baked into the shadow matrix and
    //! the sampling clamp all derive from this one value, so a border that reached only some
    //! of them — which shifts every sampled UV by its width — cannot happen.
    //!
    //! This is the whole of the shadow layer's knowledge of what an allocator block MEANS: a
    //! level and a position, turned into a rect with a gutter. The allocator holds no opinion
    //! about either.
    inline ViewRect ShadowTileRect(uint32_t node)
    {
        const ShadowTileTree::Block block = ShadowTileTree::Decode(node);
        const uint32_t gx = block.m_x;
        const uint32_t gy = block.m_y;

        // Tiles of a coarser level are wider, so the span is no longer a constant. The border
        // is: it is a fixed count of atlas texels whatever the tile's size.
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
