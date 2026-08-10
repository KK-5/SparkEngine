#pragma once

#include <cstdint>

#include "View.h"

namespace Spark::Render
{
    //! One shadow atlas split into a fixed power-of-two grid. Power-of-two, and the atlas a
    //! multiple of it, so a tile's normalized rect times the atlas extent lands exactly on
    //! integers — no half-texel drift between the viewport and the sampled UVs.
    //!
    //! Tile count is the shadow budget: it bounds the cost regardless of how many lights the
    //! scene has. Keep it in step with ViewHandleList's inline capacity (PassCapabilities.h).
    inline constexpr uint32_t kShadowAtlasResolution = 2048;
    inline constexpr uint32_t kShadowTileGrid        = 4;   // 4x4
    inline constexpr uint32_t kShadowTileCount       = kShadowTileGrid * kShadowTileGrid;
    inline constexpr uint32_t kShadowTileResolution  = kShadowAtlasResolution / kShadowTileGrid;

    //! No tile free. Distinct from slot 0, which is a perfectly good tile.
    inline constexpr uint32_t kInvalidShadowSlot = ~0u;

    //! Texels held back on each side of a tile: PCF taps at a tile's edge would otherwise
    //! reach into its neighbour. Tune together with the PCF kernel radius once sampling lands.
    inline constexpr uint32_t kShadowTileBorderTexels = 1;

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
}
