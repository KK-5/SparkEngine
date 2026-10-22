#include "DrawGroup.h"

#include <algorithm>

#include <EASTL/sort.h>
#include <EASTL/tuple.h>

namespace Spark::Render
{
    namespace
    {
        //! What the storage is sorted by first, a group's run in it.
        eastl::tuple<uint32_t, uint32_t> OwnerOf(const DrawGroup& group)
        {
            return eastl::make_tuple(static_cast<uint32_t>(group.m_pass), static_cast<uint32_t>(group.m_view));
        }
    }

    bool DrawsBefore(const PipelineVariant& a, const PipelineVariant& b)
    {
        auto order = [](const PipelineVariant& v)
        {
            return eastl::make_tuple(v.m_priority, static_cast<uint32_t>(v.m_cullMode),
                                     static_cast<uint32_t>(v.m_blendMode),
                                     static_cast<uint32_t>(v.m_featureMacros), v.m_materialShader);
        };
        return order(a) < order(b);
    }

    void GroupDrawEntries(eastl::span<DrawEntry> entries, DrawOrder order, eastl::vector<DrawGroup>& groups)
    {
        groups.clear();

        if (order == DrawOrder::Any)
        {
            eastl::sort(entries.begin(), entries.end(), [](const DrawEntry& a, const DrawEntry& b)
            {
                return DrawsBefore(a.m_variant, b.m_variant);
            });
        }

        uint32_t firstDraw = 0;
        for (DrawEntry& entry : entries)
        {
            if (groups.empty() || groups.back().m_variant != entry.m_variant)
            {
                DrawGroup group;
                group.m_index     = static_cast<uint32_t>(groups.size());
                group.m_variant   = entry.m_variant;
                group.m_firstDraw = firstDraw;
                groups.push_back(group);
            }

            DrawGroup& group = groups.back();
            entry.m_groupIndex = group.m_index;
            group.m_maxDraws  += entry.m_count;
            firstDraw         += entry.m_count;
        }
    }

    void SortDrawGroups(DrawContext& drawCtx)
    {
        drawCtx.Sort<DrawGroup>([](const DrawGroup& a, const DrawGroup& b)
        {
            return eastl::make_tuple(OwnerOf(a), a.m_index) < eastl::make_tuple(OwnerOf(b), b.m_index);
        });
    }

    DrawGroupRange DrawGroupsOf(const DrawContext& drawCtx, Pass pass, RHI::RHIHandle view)
    {
        const auto* storage = drawCtx.GetStorage<DrawGroup>();
        if (storage == nullptr)
        {
            return {};
        }

        DrawGroup probe;
        probe.m_pass = pass;
        probe.m_view = view;
        // std's: EASTL's algorithms do not take entt's iterators as random access.
        const auto [first, last] = std::equal_range(storage->cbegin(), storage->cend(), probe,
            [](const DrawGroup& a, const DrawGroup& b) { return OwnerOf(a) < OwnerOf(b); });
        return { first, last };
    }
}
