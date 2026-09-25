#include "RenderGraphBuilder.h"

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/PassCapabilities.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>

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

    void RenderGraphBuilder::ImportResource(const RHI::AttachmentId& name, RHIHandle resource)
    {
        ASSERT(m_currentPass != NullPass, "BeginPass must be called before declaring resources.");
        ASSERT(resource != NullHandle, "Import of {}: the resource is NullHandle.", name.GetCStr());
        auto& rhiContext = *RHIExecuteContext::Current();

        if (!rhiContext.Has<ImportedTag>(resource))
        {
            rhiContext.Add<ImportedTag>(resource);
        }

        // Single-frame imports get their backing from the owning Image / Buffer component, once.
        // Per-frame resources (ImagePerFrame, the swap chain) have it refreshed every frame by
        // RenderGraph::RefreshPerFrameBackings before Build. Views come from the resource's
        // view cache on demand.
        if (auto* img = rhiContext.TryGet<Image>(resource))
        {
            if (!rhiContext.Has<BackingImage>(resource))
            {
                rhiContext.Add<BackingImage>(resource, BackingImage{ img->m_image.get() });
            }
        }
        else if (auto* buf = rhiContext.TryGet<Buffer>(resource))
        {
            if (!rhiContext.Has<BackingBuffer>(resource))
            {
                rhiContext.Add<BackingBuffer>(resource, BackingBuffer{ buf->m_buffer.get() });
            }
        }

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<BackingImage>(resource) || rhiContext.Has<BackingBuffer>(resource),
                "Imported resource {} has no backing. Single-frame: attach an Image / Buffer "
                "component before importing; per-frame: ensure RefreshPerFrameBackings ran.",
                name.GetCStr());
        }

        const auto [it, inserted] = m_resources.emplace(name, ResourceEntry{ resource, 0 });
        ASSERT(inserted || it->second.m_resource == resource,
            "{} is already declared for another resource.", name.GetCStr());
    }

    RHIHandle RenderGraphBuilder::AddPreviousFrameAttachment(ImagePassAttachment attachment, RHIHandle scope)
    {
        const RHI::AttachmentId& name = attachment.m_attachmentId.m_id;
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle declared = FindTransientImage(name);
        ASSERT(declared != NullHandle,
            "Previous-frame read of {} before any pass created it as a transient image. "
            "Declare the producing pass first.",
            name.GetCStr());
        if (declared == NullHandle)
        {
            return NullHandle;
        }

        // This frame's resource is read back next frame, whatever its producer asked for.
        auto& desc = rhiContext.Get<RHI::ImageDescriptor>(declared);
        desc.m_bindFlags |= RHI::ImageBindFlags::ShaderRead;
        rhiContext.AddOrReplace<ExtractedImage>(declared);

        RHIHandle previous = FindPreviousFrameImage(name);
        if (previous != NullHandle && !IsSameImageStorage(rhiContext.Get<RHI::ImageDescriptor>(previous), desc))
        {
            rhiContext.Remove<PreviousFrameOf>(previous);
            previous = NullHandle;
        }

        // A real previous frame is never Active; one that is, is an earlier reader's stand-in.
        const bool missing = previous == NullHandle || rhiContext.Has<PooledImageActiveTag>(previous);
        if (previous == NullHandle)
        {
            ASSERT(m_imagePool != nullptr, "[RenderGraphBuilder] No image pool for previous-frame reads.");
            previous = AcquirePooledImage(rhiContext, *m_imagePool, desc,
                rhiContext.TryGet<RHI::ClearValue>(declared), name);
            // Other readers of this name this frame share the stand-in.
            rhiContext.Add<PreviousFrameOf>(previous, PreviousFrameOf{ name });
        }

        // No version bump: nothing writes the previous frame's copy. Its id (frame offset 1)
        // is a key of its own, so BuildGraph sees readers and no writer and emits no edge.
        attachment.m_attachmentId = AttachmentId{ name, 0, 1 };
        attachment.m_access       = RHI::AttachmentAccess::Read;
        attachment.m_pass         = m_currentPass;
        attachment.m_image        = previous;

        const RHIHandle handle = AddImageAttachment(attachment, scope, nullptr);
        rhiContext.Add<PreviousFrameTag>(handle);
        if (missing)
        {
            rhiContext.Add<PreviousFrameMissingTag>(handle);
        }
        if (attachment.m_stage == RHI::AttachmentStage::Uninitialized)
        {
            m_unstagedAttachments.push_back(handle);
        }
        return handle;
    }

    RHIHandle RenderGraphBuilder::AddScopeItem(RHIHandle scope)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle item = rhiContext.CreateEntity();
        rhiContext.Add<ScopeItem>(item, ScopeItem{ scope });
        return item;
    }

    void RenderGraphBuilder::AddScopeSelection(RHIHandle scope, ScopeSelections::Collect collect)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        auto*  component  = rhiContext.TryGet<ScopeSelections>(scope);
        auto& collects   = (component != nullptr ? *component : rhiContext.Add<ScopeSelections>(scope)).m_collects;
        for (ScopeSelections::Collect existing : collects)
        {
            ASSERT(existing != collect, "The same set is selected twice in one Scope.");
        }
        collects.push_back(collect);
    }

    const RHI::PipelineLayoutDescriptor& RenderGraphBuilder::CurrentPassLayout() const
    {
        auto& passContext = *PassExecuteContext::Current();
        const auto* layout = passContext.TryGet<PassPipelineLayout>(m_currentPass);
        ASSERT(layout != nullptr && layout->m_layout,
            "Pass {} binds shader inputs but has no shaders to reflect them from.",
            passContext.Get<PassName>(m_currentPass).m_name.GetCStr());
        return *layout->m_layout;
    }

    namespace
    {
        //! The attachment stages of the shader stages in `mask`.
        RHI::AttachmentStage ToAttachmentStage(RHI::ShaderStageMask mask)
        {
            ASSERT(!CheckBitsAny(mask, RHI::ShaderStageMask::Geometry | RHI::ShaderStageMask::RayTracing),
                "Binding an attachment to a geometry or ray tracing shader input: AttachmentStage has no stage for it yet.");
            RHI::AttachmentStage stage = RHI::AttachmentStage::Uninitialized;
            if (CheckBitsAny(mask, RHI::ShaderStageMask::Vertex))
            {
                stage |= RHI::AttachmentStage::VertexShader;
            }
            if (CheckBitsAny(mask, RHI::ShaderStageMask::Fragment))
            {
                stage |= RHI::AttachmentStage::FragmentShader;
            }
            if (CheckBitsAny(mask, RHI::ShaderStageMask::Compute))
            {
                stage |= RHI::AttachmentStage::ComputeShader;
            }
            return stage;
        }
    }

    void RenderGraphBuilder::BindShaderInput(RHIHandle attachment, const RHI::InputName& input)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        auto* image = rhiContext.TryGet<ImagePassAttachment>(attachment);
        ASSERT(image != nullptr, "Binding {} to a buffer: buffer bindings are not supported yet.", input.GetCStr());
        ASSERT(!rhiContext.Has<ShaderInputBinding>(attachment),
            "The access of {} is already bound; declare another access to bind another input.",
            image->m_attachmentId.m_id.GetCStr());

        const RHI::ShaderInputImageDescriptor* desc = CurrentPassLayout().FindImageDescriptor(input);
        ASSERT(desc != nullptr, "The pass's shaders have no image input {}.", input.GetCStr());
        ASSERT(desc->m_spaceId == kPerPassSpaceId,
            "{} is in space {}; only per-pass inputs (space {}) can be bound from a Scope.",
            input.GetCStr(), desc->m_spaceId, kPerPassSpaceId);

        const bool writes = (image->m_access & RHI::AttachmentAccess::Write) != RHI::AttachmentAccess::Unknown;
        ASSERT(writes == (desc->m_access == RHI::ShaderInputImageAccess::ReadWrite),
            "{} is {} in the shader but the access {} it.",
            input.GetCStr(), writes ? "read-only" : "read-write", writes ? "writes" : "only reads");

        const RHI::AttachmentStage stage = ToAttachmentStage(desc->m_stageMask);
        if (image->m_stage == RHI::AttachmentStage::Uninitialized)
        {
            image->m_stage = stage;
        }
        else
        {
            ASSERT(image->m_stage == stage,
                "The stage declared for {} differs from the stages its shaders use it in.", input.GetCStr());
        }

        rhiContext.Add<ShaderInputBinding>(attachment, ShaderInputBinding{ input });
    }

    void RenderGraphBuilder::AddScopeSampler(RHIHandle scope, const RHI::InputName& input, const RHI::SamplerState& state)
    {
        const RHI::ShaderInputSamplerDescriptor* desc = CurrentPassLayout().FindSamplerDescriptor(input);
        ASSERT(desc != nullptr, "The pass's shaders have no sampler {}.", input.GetCStr());
        ASSERT(desc->m_spaceId == kPerPassSpaceId,
            "{} is in space {}; only per-pass samplers (space {}) can be set from a Scope.",
            input.GetCStr(), desc->m_spaceId, kPerPassSpaceId);

        auto& rhiContext = *RHIExecuteContext::Current();
        auto*  component  = rhiContext.TryGet<ScopeSamplers>(scope);
        auto& samplers   = (component != nullptr ? *component : rhiContext.Add<ScopeSamplers>(scope)).m_samplers;
        for (const ScopeSampler& sampler : samplers)
        {
            ASSERT(sampler.m_input != input, "Sampler {} is set twice in one Scope.", input.GetCStr());
        }
        samplers.push_back(ScopeSampler{ input, state });
    }

    void RenderGraphBuilder::AddScopeConstant(
        RHIHandle scope, const RHI::InputName& input, const void* bytes, uint32_t byteCount)
    {
        const RHI::ShaderInputConstantDescriptor* desc = CurrentPassLayout().FindConstantDescriptor(input);
        ASSERT(desc != nullptr, "The pass's shaders have no constant {}.", input.GetCStr());
        ASSERT(desc->m_spaceId == kPerPassSpaceId,
            "{} is in space {}; only per-pass constants (space {}) can be set from a Scope.",
            input.GetCStr(), desc->m_spaceId, kPerPassSpaceId);
        ASSERT(byteCount == desc->m_elementCount * desc->m_elementByteSize,
            "Constant {} takes {} bytes, given {}.",
            input.GetCStr(), desc->m_elementCount * desc->m_elementByteSize, byteCount);
        ASSERT(byteCount <= ScopeConstant::ByteCountMax,
            "Constant {} is {} bytes; a Scope constant holds at most {}.",
            input.GetCStr(), byteCount, ScopeConstant::ByteCountMax);

        auto& rhiContext = *RHIExecuteContext::Current();
        auto*  component  = rhiContext.TryGet<ScopeConstants>(scope);
        auto& constants  = (component != nullptr ? *component : rhiContext.Add<ScopeConstants>(scope)).m_constants;
        for (const ScopeConstant& constant : constants)
        {
            ASSERT(constant.m_input != input, "Constant {} is set twice in one Scope.", input.GetCStr());
        }
        ScopeConstant constant;
        constant.m_input     = input;
        constant.m_byteCount = byteCount;
        memcpy(constant.m_bytes.data(), bytes, byteCount);
        constants.push_back(constant);
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

        // A pass that declared nothing is not in the graph and never runs, so its Scopes and
        // their items have no place in the stream. Any attachment would have made the pass a
        // node, so such a Scope has none.
        {
            auto& rhiContext  = *RHIExecuteContext::Current();
            auto& passContext = *PassExecuteContext::Current();

            eastl::vector<RHIHandle> orphans;
            for (auto [handle, scope] : rhiContext.GetView<Scope>().each())
            {
                if (!passContext.Has<PassGlobalTimeline>(scope.m_pass))
                {
                    orphans.push_back(handle);
                }
            }
            for (auto [handle, item] : rhiContext.GetView<ScopeItem>().each())
            {
                if (!passContext.Has<PassGlobalTimeline>(rhiContext.Get<Scope>(item.m_scope).m_pass))
                {
                    orphans.push_back(handle);
                }
            }
            for (RHIHandle handle : orphans)
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
