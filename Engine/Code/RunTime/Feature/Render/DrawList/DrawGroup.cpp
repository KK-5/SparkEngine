#include "DrawGroup.h"

#include <EASTL/sort.h>
#include <EASTL/tuple.h>

namespace Spark::Render
{
    bool DrawsBefore(const PipelineVariant& a, const PipelineVariant& b)
    {
        auto order = [](const PipelineVariant& v)
        {
            return eastl::make_tuple(v.m_priority, static_cast<uint32_t>(v.m_cullMode),
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

    void CollectDrawGroups(const DrawContext& drawCtx, Pass pass, RHI::RHIHandle view, DrawGroupList& out)
    {
        out.clear();
        for (auto [entity, group] : drawCtx.GetView<DrawGroup>().each())
        {
            if (group.m_pass == pass && group.m_view == view)
            {
                out.push_back(&group);
            }
        }
    }
}
