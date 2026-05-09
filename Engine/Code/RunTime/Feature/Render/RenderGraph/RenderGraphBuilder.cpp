#include "RenderGraphBuilder.h"

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{

    void RenderGraphBuilder::Begin(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
    }

    void RenderGraphBuilder::BeginPass(Pass pass)
    {
        ASSERT(pass != NullPass, "BeginPass called with NullPass.");
        ASSERT(m_currentPass == NullPass,
            "BeginPass called while another pass scope is still active.");
        m_currentPass = pass;
    }

    void RenderGraphBuilder::EndPass()
    {
        ASSERT(m_currentPass != NullPass, "EndPass called without an active pass scope.");
        m_currentPass = NullPass;
    }

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

            // Add PassPredecessors and PassSuccessors for compiling
            auto& passContext = *PassExecuteContext::Current();

            auto pred = passContext.TryGet<PassPredecessors>(to);
            if (pred)
            {
                pred->m_preds.push_back(from);
            }
            else
            {
                passContext.Add<PassPredecessors>(to, PassPredecessors{ {from} });
            }

            auto suc = passContext.TryGet<PassSuccessors>(from);
            if (suc)
            {
                suc->m_succs.push_back(to);
            }
            else
            {
                passContext.Add<PassSuccessors>(from, PassSuccessors{ {to} });
            }
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

    eastl::vector<Pass> RenderGraphBuilder::TopoSort()
    {
        auto& passContext = *PassExecuteContext::Current();

        eastl::vector<Pass> result;
        result.reserve(m_graph.size());

        eastl::vector<Pass> ready;
        ready.reserve(m_graph.size());

        for (const auto& [pass, node] : m_graph)
        {
            if (node.inDegree == 0)
            {
                ready.push_back(pass);
            }
        }

        while(!ready.empty())
        {
            Pass cur = ready.back();
            ready.pop_back();

            passContext.Add<PassGlobalTimeline>(cur, PassGlobalTimeline{ static_cast<uint32_t>(result.size()) });
            result.push_back(cur);

            auto nodeIt = m_graph.find(cur);
            if (nodeIt == m_graph.end())
            {
                continue;
            }

            for (Pass dep : nodeIt->second.dependents)
            {
                auto depNodeIt = m_graph.find(dep);
                ASSERT(depNodeIt != m_graph.end(), "Dependency pass {} not found in graph.", passContext.Get<PassName>(dep).m_name.GetCStr());
                ASSERT(depNodeIt->second.inDegree > 0, "Invalid indegree state in topo sort.");
                if (--depNodeIt->second.inDegree == 0)
                {
                    ready.push_back(dep);
                }
            }
        }

        ASSERT(result.size() == m_graph.size(), "RenderGraph contains cycle, topo sort failed.");
        return result;
    }

    eastl::vector<Pass> RenderGraphBuilder::End()
    {
        ASSERT(m_currentPass == NullPass,
            "End() called with an active pass scope; missing EndPass?");
        BuildGraph();
        eastl::vector<Pass> passes = TopoSort();

        // Frame-scoped state: cleared at frame end, not next frame's start —
        // so a missed Begin() can't drag stale data forward.
        m_graph.clear();
        m_attachmentUses.clear();
        m_latestVersions.clear();
        m_currentPass = NullPass;

        return passes;
    }
}
