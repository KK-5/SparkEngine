#include "RenderGraphBuilder.h"

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/PassCapabilities.h>

namespace Spark::Render
{

    void RenderGraphBuilder::Begin(uint32_t frameIndex, RHI::RHIHandle swapChainResource,
                                   const Math::Vector2Int& renderSize, const Math::Vector2Int& outputSize)
    {
        m_frameIndex = frameIndex;
        m_renderSize = renderSize;
        m_outputSize = outputSize;
        m_curSwapChainResource = swapChainResource;
    }

    void RenderGraphBuilder::BeginPass(Pass pass)
    {
        ASSERT(pass != NullPass, "BeginPass called with NullPass.");
        ASSERT(m_currentPass == NullPass,
            "BeginPass called while another pass scope is still active.");
        m_currentPass       = pass;
        m_currentScope      = NullHandle;
        m_currentColorCount = 0;
    }

    void RenderGraphBuilder::EndPass()
    {
        ASSERT(m_currentPass != NullPass, "EndPass called without an active pass scope.");

        if constexpr (s_buildValidation)
        {
            auto& rhiContext  = *RHIExecuteContext::Current();
            auto& passContext = *PassExecuteContext::Current();
            const char* passName = passContext.Get<PassName>(m_currentPass).m_name.GetCStr();

            for (const OpenedScope& opened : m_passScopes)
            {
                ASSERT(opened.m_attachmentCount > 0,
                    "Pass {} opened Scope #{} but declared no attachment in it.",
                    passName, rhiContext.Get<Scope>(opened.m_scope).m_index);
            }

            for (RHIHandle attachment : m_unstagedAttachments)
            {
                const auto* image  = rhiContext.TryGet<ImagePassAttachment>(attachment);
                const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(attachment);
                const RHI::AttachmentStage stage = image ? image->m_stage : buffer->m_stage;
                ASSERT(stage != RHI::AttachmentStage::Uninitialized,
                    "Pass {}: the shader access of {} has no stage. Give it one with .Stage(...).",
                    passName, image ? image->m_attachmentId.m_id.GetCStr() : buffer->m_attachmentId.m_id.GetCStr());
            }

            if (m_passScopes.size() > 1)
            {
                const auto* caps = passContext.TryGet<PassCapabilities>(m_currentPass);
                ASSERT(caps == nullptr || caps->m_collectViews == nullptr,
                    "Pass {} renders a view and has {} Scopes; a pass that renders a view has one.",
                    passName, static_cast<uint32_t>(m_passScopes.size()));
            }
        }

        m_currentPass       = NullPass;
        m_currentScope      = NullHandle;
        m_currentColorCount = 0;
        m_passScopes.clear();
        m_unstagedAttachments.clear();
    }

    RHIHandle RenderGraphBuilder::OpenScope()
    {
        ASSERT(m_currentPass != NullPass, "A Scope can only be opened inside a pass.");
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        const RHIHandle scope = rhiContext.CreateEntity();
        rhiContext.Add<Scope>(scope, Scope{
            m_currentPass,
            static_cast<uint32_t>(m_passScopes.size()),
            passContext.Get<PassExecuteQueue>(m_currentPass).m_queue });
        m_passScopes.push_back(OpenedScope{ scope, 0 });
        return scope;
    }

    RHIHandle RenderGraphBuilder::CurrentScope()
    {
        if (m_currentScope == NullHandle)
        {
            m_currentScope = OpenScope();
        }
        return m_currentScope;
    }

    void RenderGraphBuilder::CreateImage(const RHI::AttachmentId& name, const RHI::ImageDescriptor& desc)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring resources.");
            ASSERT(m_resources.find(name) == m_resources.end(),
                "AttachmentId {} has already been declared (Create / Import).",
                name.GetCStr());
        }
        m_resources.emplace(name, ResourceEntry{ CreateTransientImageResource(name, desc, nullptr), 0 });
    }

    void RenderGraphBuilder::CreateBuffer(const RHI::AttachmentId& name, const RHI::BufferDescriptor& desc)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring resources.");
            ASSERT(m_resources.find(name) == m_resources.end(),
                "AttachmentId {} has already been declared (Create / Import).",
                name.GetCStr());
        }
        m_resources.emplace(name, ResourceEntry{ CreateTransientBufferResource(name, desc), 0 });
    }

    RHIHandle RenderGraphBuilder::AddScopeAttachment(
        RHIHandle scope, uint32_t* colorCount, const RHI::AttachmentId& name,
        RHI::AttachmentUsage usage, RHI::AttachmentAccess access, RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction* action)
    {
        auto it = m_resources.find(name);
        ASSERT(it != m_resources.end(),
            "AttachmentId {} has not been declared (Create / Import) yet. Passes must be "
            "declared in dependency order, an access after the Create / Import of its resource.",
            name.GetCStr());
        ResourceEntry& entry = it->second;

        uint32_t version = entry.m_latestVersion;
        if ((access & RHI::AttachmentAccess::Write) != RHI::AttachmentAccess::Unknown)
        {
            m_attachmentUses[AttachmentId{ name, version }].emplace_back(m_currentPass, RHI::AttachmentAccess::Read);
            version = ++entry.m_latestVersion;
        }

        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle resource = entry.m_resource;
        RHIHandle attachment = NullHandle;
        if (rhiContext.Has<RHI::BufferDescriptor>(resource) || rhiContext.Has<BackingBuffer>(resource))
        {
            BufferPassAttachment a;
            a.m_attachmentId = AttachmentId{ name, version };
            a.m_access       = access;
            a.m_usage        = usage;
            a.m_stage        = stage;
            a.m_buffer       = resource;
            a.m_pass         = m_currentPass;
            attachment = AddBufferAttachment(a, scope);
        }
        else
        {
            ImagePassAttachment a;
            a.m_attachmentId = AttachmentId{ name, version };
            a.m_access       = access;
            a.m_usage        = usage;
            a.m_stage        = stage;
            a.m_image        = resource;
            a.m_pass         = m_currentPass;
            if (action != nullptr)
            {
                a.m_action = *action;
                if (action->m_loadAction == RHI::AttachmentLoadAction::Clear
                    && rhiContext.Has<TransientTag>(resource) && !rhiContext.Has<RHI::ClearValue>(resource))
                {
                    rhiContext.Add<RHI::ClearValue>(resource, action->m_clearValue);
                }
            }
            attachment = AddImageAttachment(a, scope, colorCount);
        }

        if (stage == RHI::AttachmentStage::Uninitialized)
        {
            m_unstagedAttachments.push_back(attachment);
        }
        return attachment;
    }

    RHIHandle RenderGraphBuilder::AddImageAttachment(
        const ImagePassAttachment& attachment, RHIHandle scope, uint32_t* colorCount)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(handle, attachment);
        rhiContext.Add<ScopeAttachment>(handle, ScopeAttachment{ scope });
        if (attachment.m_usage == RHI::AttachmentUsage::RenderTarget)
        {
            ASSERT(colorCount != nullptr, "A render target outside a render pass Scope.");
            rhiContext.Add<ColorAttachmentIndex>(handle, ColorAttachmentIndex{ (*colorCount)++ });
        }
        m_attachmentUses[attachment.m_attachmentId].emplace_back(
            attachment.m_pass,
            NormalizeImageAccess(attachment.m_access, attachment.m_action));
        CountScopeAttachment(scope);
        return handle;
    }

    RHIHandle RenderGraphBuilder::AddBufferAttachment(const BufferPassAttachment& attachment, RHIHandle scope)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(handle, attachment);
        rhiContext.Add<ScopeAttachment>(handle, ScopeAttachment{ scope });
        m_attachmentUses[attachment.m_attachmentId].emplace_back(attachment.m_pass, attachment.m_access);
        CountScopeAttachment(scope);
        return handle;
    }

    void RenderGraphBuilder::CountScopeAttachment(RHIHandle scope)
    {
        for (OpenedScope& opened : m_passScopes)
        {
            if (opened.m_scope == scope)
            {
                ++opened.m_attachmentCount;
                return;
            }
        }
        ASSERT(false, "Attachment added to a Scope the current pass did not open.");
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

        // A pass that declared nothing is not in the graph and never runs, so its Scope has no
        // place in the stream. Any declaration would have made the pass a node, so such a
        // Scope has no attachments either.
        {
            auto& rhiContext  = *RHIExecuteContext::Current();
            auto& passContext = *PassExecuteContext::Current();

            eastl::vector<RHIHandle> orphanScopes;
            for (auto [handle, scope] : rhiContext.GetView<Scope>().each())
            {
                if (!passContext.Has<PassGlobalTimeline>(scope.m_pass))
                {
                    orphanScopes.push_back(handle);
                }
            }
            for (RHIHandle handle : orphanScopes)
            {
                rhiContext.DestoryEntity(handle);
            }
        }

        // Frame-scoped state: cleared at frame end, not next frame's start —
        // so a missed Begin() can't drag stale data forward.
        m_graph.clear();
        m_attachmentUses.clear();
        m_resources.clear();
        m_currentPass  = NullPass;
        m_currentScope = NullHandle;

        return passes;
    }
}
