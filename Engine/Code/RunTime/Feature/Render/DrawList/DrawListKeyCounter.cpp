#include "DrawListKeyCounter.h"

#include <Log/ILogSystem.h>
#include <Math/Bit.h>

#include <PipelineState/PipelineKeyIndex.h>

#include "DrawList.h"

namespace Spark::Render
{
    DrawListKeyCounter::DrawListKeyCounter()
    {
        // The key's own value is its index for now, so every index is below the key space.
        m_keyCounts.resize(PipelineKeyCount);
    }

    void DrawListKeyCounter::Count(PipelineKey key, DrawMask drawMask)
    {
        const PipelineKeyIndex index = PipelineKeyIndexOf(key);
        ASSERT(index.m_value < m_keyCounts.size(), "[DrawListKeyCounter] Key index {} is past the counts.", index.m_value);

        // A CPU-culled list is its instances (DrawListInstance), not its keys.
        const DrawMask counted = drawMask & ~CpuCulledDrawMask;

        KeyCounts& keyCounts = m_keyCounts[index.m_value];
        keyCounts.m_key = key;
        for (uint32_t bit = 0; bit < DrawMaskBitCount; ++bit)
        {
            if (CheckBit(counted, bit))
            {
                ++keyCounts.m_counts[bit];
            }
        }
    }

    void DrawListKeyCounter::Publish(DrawContext& drawCtx)
    {
        for (uint32_t index = 0; index < static_cast<uint32_t>(m_keyCounts.size()); ++index)
        {
            KeyCounts& keyCounts = m_keyCounts[index];
            for (uint32_t bit = 0; bit < DrawMaskBitCount; ++bit)
            {
                if (keyCounts.m_counts[bit] == 0)
                {
                    continue;
                }

                DrawListKey listKey;
                listKey.m_drawMaskBitIndex = bit;
                listKey.m_keyIndex         = PipelineKeyIndex{ index };
                listKey.m_key              = keyCounts.m_key;
                listKey.m_instanceCount    = keyCounts.m_counts[bit];
                drawCtx.Add<DrawListKey>(drawCtx.CreateEntity(), listKey);
            }
            keyCounts.m_counts.fill(0);
        }
    }
}
