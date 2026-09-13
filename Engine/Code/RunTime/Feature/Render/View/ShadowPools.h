#pragma once

#include <EASTL/array.h>
#include <EASTL/bitset.h>
#include <EASTL/fixed_vector.h>

#include <Handle/HandlePool.h>

#include "ShadowAtlasLayout.h"

namespace Spark::Render
{
    class ShadowTilePool;
    class ShadowRowPool;

    //! The allocator's record of where in the atlas a shadow view rasterizes. Opaque —
    //! nothing outside ShadowViewSystem may derive anything from Get(), least of all an
    //! array index. The rect it produced lives on View::m_rect.
    //!
    //! Owning: the view entity carrying it is what keeps the tile allocated, so the tile
    //! comes back whatever destroys that entity and whenever it does.
    //!
    //! It equals ShadowViewIndex today only because both allocators are first-fit over
    //! equally sized bitsets and move in lockstep. A resolution ladder makes this an
    //! allocator node rather than a cell of a fixed grid, and the two part ways.
    using ShadowAtlasTile = SharedHandle<ShadowTilePool>;

    //! Tiles handed out together. Sized for a cube's faces, the most a light ever asks for.
    using ShadowTileList = eastl::fixed_vector<ShadowAtlasTile, kShadowCubeFaceCount>;

    //! A light's run of g_ShadowViews rows, addressed by the first. Owning, like the tile,
    //! and held by the light rather than its views because the run is allocated per light.
    using ShadowViewRows = SharedHandle<ShadowRowPool>;

    //! Space to rasterize into, cut out of the atlas by a quad tree. Holds the tree and
    //! nothing else: the atlas image stays in ShadowAtlasAllocator, because a tile is
    //! returned from a destructor that can run while any context is being torn down.
    class ShadowTilePool final : public HandlePool<ShadowTilePool>
    {
    public:
        ShadowTilePool() { Reserve(ShadowTileTree::kNodeCount); }

        //! count tiles at exactly that level, or none — a partial set rolls itself back
        //! when the handles already taken are dropped.
        bool AllocateAt(uint32_t level, uint32_t count, ShadowTileList& out);

        //! That level, or the coarsest finer one that fits all count of them. Returns the
        //! level granted, or kNoShadowLevel.
        uint32_t AllocateOrFiner(uint32_t level, uint32_t count, ShadowTileList& out);

    private:
        void Free(uint32_t tile) override
        {
            m_tiles.Free(tile);
            m_fullLogged = false;
        }

        ShadowTileTree m_tiles;

        //! Keeps the "atlas full" warning to one line per episode: allocation is retried
        //! every frame, so a light that does not fit would otherwise log forever.
        bool m_fullLogged = false;
    };

    //! Rows of g_ShadowViews. A light takes a consecutive run so one index addresses all of
    //! its faces; the run's length is the pool's to remember, which is what keeps the handle
    //! a single id like every other.
    class ShadowRowPool final : public HandlePool<ShadowRowPool>
    {
    public:
        ShadowRowPool()
        {
            Reserve(kShadowViewCapacity);
            m_runLength.fill(0);
        }

        //! count consecutive rows, or an empty handle.
        ShadowViewRows Allocate(uint32_t count);

    private:
        void Free(uint32_t base) override;

        eastl::bitset<kShadowViewCapacity>          m_rows;
        eastl::array<uint32_t, kShadowViewCapacity> m_runLength;
    };
}
