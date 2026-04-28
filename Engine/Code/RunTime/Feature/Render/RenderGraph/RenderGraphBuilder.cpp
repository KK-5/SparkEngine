#include "RenderGraphBuilder.h"

namespace Spark::Render
{
    void RenderGraphBuilder::TouchNode(Pass pass)
    {
        // 让没有任何边的孤立 pass 也出现在图里
        m_graph.try_emplace(pass);
    }


    void RenderGraphBuilder::AddEdge(Pass from, Pass to)
    {
        // 防止self edge
        if (from == to)
        {
            return;
        }

        auto [it, inserted] = m_graph[from].dependents.insert(to);
        if (inserted)
        {
            ++m_graph[to].inDegree;
        }
    }

    void RenderGraphBuilder::BuildGraph()
    {
        // merge per-pass uses on each attachment (multi-use within one pass collapses to OR'd access)
        for (auto& [id, entries] : m_attachmentUses)
        {
            eastl::unordered_map<Pass, RHI::AttachmentAccess> perPass;
            for (const auto& e : entries)
            {
                perPass[e.pass] |= e.access;
            }

            if (perPass.size() != entries.size())
            {
                entries.clear();
                entries.reserve(perPass.size());
                for (const auto& [pass, access] : perPass)
                {
                    entries.emplace_back(pass, access);
                }
            }
        }

        m_graph.clear();

        auto HasFlag = [](RHI::AttachmentAccess access, RHI::AttachmentAccess require) -> bool
        {
            return (access & require) != RHI::AttachmentAccess::Unknown;
        };

        for (auto& [id, entries] : m_attachmentUses)
        {
            Pass writer = NullPass;
            eastl::vector<Pass> readerWriters;  // RW
            eastl::vector<Pass> readers;    // R only

            readerWriters.reserve(entries.size());
            readers.reserve(entries.size());

            for (const auto& entry: entries)
            {
                const bool isRead = HasFlag(entry.access, RHI::AttachmentAccess::Read);
                const bool isWrite = HasFlag(entry.access, RHI::AttachmentAccess::Write);

                ASSERT(entry.pass != NullPass, "Attachment {} has entry with NullPass.", id.m_id.GetCStr());
                ASSERT(isRead || isWrite, "Attachment {} entry has Unknown access — likely missing access flag.", id.m_id.GetCStr());

                TouchNode(entry.pass);

                if (isRead && isWrite)
                {
                    readerWriters.push_back(entry.pass);
                }
                else if (isWrite)
                {
                    ASSERT(writer == NullPass,
                        "Attachment {} has multiple pure writers — non-commutative chain "
                        "must be expressed via renaming.",
                        id.m_id.GetCStr()
                    );
                    writer = entry.pass;
                }
                else if (isRead)
                {
                    readers.push_back(entry.pass);
                }
            }

            // All read after write
            if (writer != NullPass)
            {
                for (Pass rw : readerWriters) 
                {
                    AddEdge(writer, rw);
                }
                for (Pass r : readers)
                {
                    AddEdge(writer, r);
                }
            }

            // Read after read write
            for (Pass rw : readerWriters)
            {
                for (Pass r : readers)
                {
                    AddEdge(rw, r);
                }
            }
        }
    }
}
