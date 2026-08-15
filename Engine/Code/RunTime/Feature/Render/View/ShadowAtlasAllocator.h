#pragma once

#include <EASTL/bitset.h>

#include <RHI/Context/RHIContext.h>

#include "ShadowAtlasLayout.h"

namespace Spark::Render
{
    //! The shadow atlas image and the two things handed out against it: tiles to rasterize
    //! into, and g_ShadowViews rows to describe them. The image is here because a tile id
    //! means nothing without it.
    //!
    //! Tiles and rows are separate on purpose and have different lifetimes — a light keeps
    //! its rows while its tiles change level and face set — so they are separate calls.
    //!
    //! Knows nothing of lights, views or components: which light deserves what is
    //! ShadowViewSystem's, and this only says whether the space exists.
    class ShadowAtlasAllocator
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Shutdown(RHI::RHIContext& rhiCtx);

        //! Written by ShadowPass, read by LightingPass. Persistent, and deferred-init, so
        //! callers still have to check it is live before handing out any space.
        RHI::RHIHandle Image() const { return m_image; }

        //! count tiles at exactly that level, or none — a partial set is rolled back.
        bool AllocateTilesAt(uint32_t level, uint32_t count, uint32_t* outTiles);

        //! That level, or the coarsest finer one that fits all count of them. Returns the
        //! level granted, or kNoShadowLevel.
        uint32_t AllocateTilesOrFiner(uint32_t level, uint32_t count, uint32_t* outTiles);

        void ReleaseTile(uint32_t tile);

        static uint32_t LevelOfTile(uint32_t tile)
        {
            return ShadowTileTree::Decode(tile).m_level;
        }

        //! Consecutive rows, so one light's faces are addressable from a single base index.
        //! Returns the first, or kInvalidShadowSlot.
        uint32_t AllocateRows(uint32_t count);
        void     ReleaseRows(uint32_t base, uint32_t count);

    private:
        RHI::RHIHandle                     m_image = RHI::NullHandle;
        ShadowTileTree                     m_tiles;
        eastl::bitset<kShadowViewCapacity> m_rows;

        //! Keeps the "atlas full" warning to one line per episode: allocation is retried
        //! every frame, so a light that does not fit would otherwise log forever.
        bool m_fullLogged = false;
    };
}
