#include "DrawList.h"

#include <EASTL/algorithm.h>

#include <ECS/Common.h>
#include <Log/ILogSystem.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Command/DrawItem.h>

#include <Pass/PassContext.h>
#include <Pass/PassCapabilities.h>

namespace Spark::Render
{
    namespace
    {
        //! Rebuild from scratch through the insert path, so the batch layout is produced
        //! by the same code that maintains it incrementally.
        void FillDrawList(DrawList& list, const eastl::vector<RHI::RHIHandle>& items)
        {
            list.m_entries.clear();
            list.m_batches.clear();
            for (RHI::RHIHandle item : items)
            {
                DrawListInsert(list, item, kSingleVariantId);
            }
        }

        bool HasKey(const eastl::vector<DrawListKey>& keys, const DrawListKey& key)
        {
            return eastl::find(keys.begin(), keys.end(), key) != keys.end();
        }

        bool HasList(const PassDrawLists& lists, const DrawListKey& key)
        {
            for (const DrawList& list : lists.m_lists)
            {
                if (list.m_key == key)
                {
                    return true;
                }
            }
            return false;
        }

    }

    void DrawListInsert(DrawList& list, RHI::RHIHandle entry, uint16_t variantId)
    {
        uint32_t slot = 0;
        while (slot < list.m_batches.size() && list.m_batches[slot].m_variantId < variantId)
        {
            ++slot;
        }

        if (slot == list.m_batches.size() || list.m_batches[slot].m_variantId != variantId)
        {
            DrawBatch batch;
            batch.m_variantId = variantId;
            batch.m_begin = (slot < list.m_batches.size())
                ? list.m_batches[slot].m_begin
                : static_cast<uint32_t>(list.m_entries.size());
            batch.m_end = batch.m_begin;
            list.m_batches.insert(list.m_batches.begin() + slot, batch);
        }

        list.m_entries.push_back(RHI::NullHandle);

        // Back to front: each batch after the target one moves its first entry into the
        // slot its successor just vacated, walking the free slot down to the target.
        for (size_t i = list.m_batches.size(); i > slot + 1; --i)
        {
            DrawBatch& batch = list.m_batches[i - 1];
            list.m_entries[batch.m_end] = list.m_entries[batch.m_begin];
            ++batch.m_begin;
            ++batch.m_end;
        }

        DrawBatch& target = list.m_batches[slot];
        list.m_entries[target.m_end] = entry;
        ++target.m_end;
    }

    void DrawListRemoveAt(DrawList& list, uint32_t index)
    {
        if (index >= list.m_entries.size())
        {
            return;
        }

        uint32_t slot = 0;
        while (slot < list.m_batches.size() && list.m_batches[slot].m_end <= index)
        {
            ++slot;
        }
        if (slot == list.m_batches.size())
        {
            LOG_ERROR("[DrawList] Entry {} is not covered by any batch.", index);
            return;
        }

        DrawBatch& target = list.m_batches[slot];
        list.m_entries[index] = list.m_entries[target.m_end - 1];
        --target.m_end;
        const bool targetEmpty = target.m_begin == target.m_end;

        // Mirror of insert: the free slot walks up, each later batch giving up its last
        // entry to its own start.
        for (size_t i = slot + 1; i < list.m_batches.size(); ++i)
        {
            DrawBatch& batch = list.m_batches[i];
            list.m_entries[batch.m_begin - 1] = list.m_entries[batch.m_end - 1];
            --batch.m_begin;
            --batch.m_end;
        }

        list.m_entries.pop_back();
        if (targetEmpty)
        {
            list.m_batches.erase(list.m_batches.begin() + slot);
        }
    }

    void BuildPassDrawLists()
    {
        auto* passCtx = PassExecuteContext::Current();
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        if (!passCtx || !rhiCtx)
        {
            return;
        }

        eastl::vector<DrawListKey>    keys;
        eastl::vector<RHI::RHIHandle> items;

        passCtx->GetView<PassCapabilities>().each([&](Pass pass, const PassCapabilities& caps)
        {
            if (!caps.m_collectDrawListKeys || !caps.m_collectDrawItems)
            {
                return;   // pass renders no view — nothing to keep lists for
            }

            auto* lists = passCtx->TryGet<PassDrawLists>(pass);
            if (!lists)
            {
                lists = &passCtx->Add<PassDrawLists>(pass);
            }

            keys.clear();
            caps.m_collectDrawListKeys(*rhiCtx, keys);

            for (size_t i = lists->m_lists.size(); i > 0; --i)
            {
                if (!HasKey(keys, lists->m_lists[i - 1].m_key))
                {
                    lists->m_lists.erase(lists->m_lists.begin() + (i - 1));
                }
            }

            for (const DrawListKey& key : keys)
            {
                if (HasList(*lists, key))
                {
                    continue;
                }
                items.clear();
                caps.m_collectDrawItems(*rhiCtx, items);

                DrawList list;
                list.m_key = key;
                FillDrawList(list, items);
                lists->m_lists.push_back(eastl::move(list));
            }
        });
    }

}
