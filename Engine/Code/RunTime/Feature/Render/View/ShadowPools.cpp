#include "ShadowPools.h"

#include <Log/ILogSystem.h>

namespace Spark::Render
{
    bool ShadowTilePool::AllocateAt(uint32_t level, uint32_t count, ShadowTileList& out)
    {
        out.clear();
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t node = m_tiles.Allocate(level);
            if (node == ShadowTileTree::kInvalidNode)
            {
                out.clear();   // The handles already taken hand their tiles back here.
                return false;
            }
            out.push_back(MakeHandle(node));
        }
        return true;
    }

    uint32_t ShadowTilePool::AllocateOrFiner(uint32_t level, uint32_t count, ShadowTileList& out)
    {
        // Down to the finest, so fragmentation is handled by the same rule as a tight budget:
        // the tree can hold a free 512 while no 1024 can be cut out of it, and a light in that
        // situation should get the 512 rather than nothing.
        for (uint32_t l = level; l <= kShadowFinestLevel; ++l)
        {
            if (AllocateAt(l, count, out))
            {
                m_fullLogged = false;
                return l;
            }
        }

        if (!m_fullLogged)
        {
            LOG_WARN("[ShadowTilePool] Cannot fit {} tile(s) at any level down to {}; "
                     "further lights cast no shadow until space frees up.",
                count, kShadowFinestLevel);
            m_fullLogged = true;
        }
        return kNoShadowLevel;
    }

    ShadowViewRows ShadowRowPool::Allocate(uint32_t count)
    {
        if (count == 0 || count > kShadowViewCapacity)
        {
            return ShadowViewRows{};
        }

        uint32_t base = 0;
        while (base + count <= kShadowViewCapacity)
        {
            uint32_t free = 0;
            while (free < count && !m_rows.test(base + free))
            {
                ++free;
            }
            if (free == count)
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    m_rows.set(base + i);
                }
                m_runLength[base] = count;
                return MakeHandle(base);
            }
            base += free + 1;   // Row base + free is taken, so no run can start before it.
        }
        return ShadowViewRows{};
    }

    void ShadowRowPool::Free(uint32_t base)
    {
        for (uint32_t i = 0; i < m_runLength[base]; ++i)
        {
            m_rows.set(base + i, false);
        }
        m_runLength[base] = 0;
    }
}
