#include "ShadowAtlasAllocator.h"

#include <Log/ILogSystem.h>

#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

namespace Spark::Render
{
    void ShadowAtlasAllocator::Init(RHI::RHIContext& rhiCtx)
    {
        auto desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::DepthStencil | RHI::ImageBindFlags::ShaderRead,
            kShadowAtlasResolution, kShadowAtlasResolution, kShadowAtlasFormat);
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        m_image = RHI::CreateImportedImage(rhiCtx, ObjectName("ShadowAtlas"), desc);
        rhiCtx.Add<ShadowAtlasTag>(m_image);
    }

    void ShadowAtlasAllocator::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_image != RHI::NullHandle && rhiCtx.Valid(m_image))
        {
            rhiCtx.DestoryEntity(m_image);
        }
        m_image = RHI::NullHandle;

        m_tiles.Reset();
        m_rows.reset();
        m_fullLogged = false;
    }

    bool ShadowAtlasAllocator::AllocateTilesAt(uint32_t level, uint32_t count, uint32_t* outTiles)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t node = m_tiles.Allocate(level);
            if (node == ShadowTileTree::kInvalidNode)
            {
                for (uint32_t taken = 0; taken < i; ++taken)
                {
                    m_tiles.Free(outTiles[taken]);
                }
                return false;
            }
            outTiles[i] = node;
        }
        return true;
    }

    uint32_t ShadowAtlasAllocator::AllocateTilesOrFiner(
        uint32_t level, uint32_t count, uint32_t* outTiles)
    {
        // Down to the finest, so fragmentation is handled by the same rule as a tight budget:
        // the tree can hold a free 512 while no 1024 can be cut out of it, and a light in that
        // situation should get the 512 rather than nothing.
        for (uint32_t l = level; l <= kShadowFinestLevel; ++l)
        {
            if (AllocateTilesAt(l, count, outTiles))
            {
                m_fullLogged = false;
                return l;
            }
        }

        if (!m_fullLogged)
        {
            LOG_WARN("[ShadowAtlasAllocator] Cannot fit {} tile(s) at any level down to {}; "
                     "further lights cast no shadow until space frees up.",
                count, kShadowFinestLevel);
            m_fullLogged = true;
        }
        return kNoShadowLevel;
    }

    void ShadowAtlasAllocator::ReleaseTile(uint32_t tile)
    {
        m_tiles.Free(tile);
        m_fullLogged = false;
    }

    uint32_t ShadowAtlasAllocator::AllocateRows(uint32_t count)
    {
        if (count == 0 || count > kShadowViewCapacity)
        {
            return kInvalidShadowSlot;
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
                return base;
            }
            base += free + 1;   // Row base + free is taken, so no run can start before it.
        }
        return kInvalidShadowSlot;
    }

    void ShadowAtlasAllocator::ReleaseRows(uint32_t base, uint32_t count)
    {
        for (uint32_t i = 0; i < count && base + i < kShadowViewCapacity; ++i)
        {
            m_rows.set(base + i, false);
        }
    }
}
