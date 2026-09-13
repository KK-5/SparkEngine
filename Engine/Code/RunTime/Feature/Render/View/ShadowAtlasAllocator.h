#pragma once

#include <RHI/Context/RHIContext.h>

#include "ShadowPools.h"

namespace Spark::Render
{
    //! The shadow atlas image and the two pools handed out against it: tiles to rasterize
    //! into, and g_ShadowViews rows to describe them. The image is here because a tile id
    //! means nothing without it, and because a pool may hold no GPU resource — its ids come
    //! back from handle destructors.
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

        bool AllocateTilesAt(uint32_t level, uint32_t count, ShadowTileList& out)
        {
            return m_tiles->AllocateAt(level, count, out);
        }

        uint32_t AllocateTilesOrFiner(uint32_t level, uint32_t count, ShadowTileList& out)
        {
            return m_tiles->AllocateOrFiner(level, count, out);
        }

        ShadowViewRows AllocateRows(uint32_t count) { return m_rows->Allocate(count); }

        static uint32_t LevelOfTile(uint32_t tile)
        {
            return ShadowTileTree::Decode(tile).m_level;
        }

    private:
        RHI::RHIHandle      m_image = RHI::NullHandle;
        Ptr<ShadowTilePool> m_tiles{ new ShadowTilePool() };
        Ptr<ShadowRowPool>  m_rows { new ShadowRowPool() };
    };
}
