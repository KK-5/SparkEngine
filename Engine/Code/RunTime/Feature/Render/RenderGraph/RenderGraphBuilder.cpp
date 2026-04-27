#include "RenderGraphBuilder.h"

namespace Spark::Render
{
    void RenderGraphBuilder::BuildGraph()
    {
        // merge attachment uses
        for (auto& [id, entrys] : m_attachmentUses)
        {
            eastl::unordered_map<Pass, RHI::AttachmentAccess> passAccessMap;
            bool hasMerge = false;
            
            for (AttachmentEntry& entry: entrys)
            {
                auto it = passAccessMap.find(entry.pass);
                if (it != passAccessMap.end())
                {
                    it->second |= entry.access;
                    hasMerge = true;
                }
                else
                {
                    passAccessMap.emplace(entry.pass, entry.access);
                }
            }

            if (hasMerge)
            {
                eastl::vector<AttachmentEntry> mergedEntrys;
                mergedEntrys.reserve(passAccessMap.size());
                eastl::transform(passAccessMap.begin(), passAccessMap.end(), eastl::back_inserter(mergedEntrys), [](const auto& pair)
                {
                    return AttachmentEntry{pair.first, pair.second};
                });

                m_attachmentUses[id] = mergedEntrys;
            }
        }

        for (auto& [id, entrys] : m_attachmentUses)
        {
            
        }
    }
}