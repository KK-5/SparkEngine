#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/PassCapabilities.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Command/DispatchItem.h>

namespace Spark::Render
{
    namespace
    {
        const AttachmentId& GetAttachmentId(const RHIContext& context, RHIHandle attachment)
        {
            const auto* image = context.TryGet<ImagePassAttachment>(attachment);
            return image ? image->m_attachmentId : context.Get<BufferPassAttachment>(attachment).m_attachmentId;
        }

        //! Whether two attachments are of one resource. By the resource where both are linked to
        //! theirs, which also finds one imported under two names; by the name where the resource
        //! is declared later, or is last frame's copy.
        bool AreOfOneResource(const RHIContext& context, RHIHandle lhs, RHIHandle rhs)
        {
            const RHIHandle lhsResource = context.Get<ScopeAttachment>(lhs).m_resource;
            const RHIHandle rhsResource = context.Get<ScopeAttachment>(rhs).m_resource;
            if (lhsResource != NullHandle && rhsResource != NullHandle)
            {
                return lhsResource == rhsResource;
            }

            const AttachmentId& lhsId = GetAttachmentId(context, lhs);
            const AttachmentId& rhsId = GetAttachmentId(context, rhs);
            return lhsId.m_id == rhsId.m_id && lhsId.m_frameOffset == rhsId.m_frameOffset;
        }

        //! Whether two attachments of one resource ask one subresource for accesses that
        //! conflict. A buffer has no subresources: it is asked for as a whole.
        bool AccessesConflict(const RHIContext& context, RHIHandle lhs, RHIHandle rhs)
        {
            const auto* lhsImage = context.TryGet<ImagePassAttachment>(lhs);
            const auto* rhsImage = context.TryGet<ImagePassAttachment>(rhs);
            if (lhsImage != nullptr && rhsImage != nullptr)
            {
                // Depth slices are not subresources, and the overlap does not look at them.
                return lhsImage->m_viewDescriptor.OverlapsSubResource(rhsImage->m_viewDescriptor)
                    && RHI::HasAccessConflict(ConvertAttachmentAccess(*lhsImage), ConvertAttachmentAccess(*rhsImage));
            }
            if (lhsImage != nullptr || rhsImage != nullptr)
            {
                return false;
            }
            return RHI::HasAccessConflict(
                ConvertAttachmentAccess(context.Get<BufferPassAttachment>(lhs)),
                ConvertAttachmentAccess(context.Get<BufferPassAttachment>(rhs)));
        }

        bool IsBufferResource(const RHIContext& context, RHIHandle resource)
        {
            return context.Has<RHI::BufferDescriptor>(resource) || context.Has<BackingBuffer>(resource);
        }

        //! A depth-stencil view binds both planes whatever aspects it names, so the attachment
        //! must name every aspect the image has.
        bool CoversDepthStencilAspects(const RHIContext& context, const ImagePassAttachment& attachment)
        {
            if (attachment.m_usage != RHI::AttachmentUsage::DepthStencil)
            {
                return true;
            }

            const RHI::ImageDescriptor* descriptor = context.TryGet<RHI::ImageDescriptor>(attachment.m_image);
            if (!descriptor)
            {
                const auto* backing = context.TryGet<BackingImage>(attachment.m_image);
                descriptor = (backing && backing->m_image) ? &backing->m_image->GetDescriptor() : nullptr;
            }
            if (!descriptor)
            {
                return true;
            }
            const RHI::ImageAspectFlags aspects = RHI::GetImageAspectFlags(descriptor->m_format);
            return (attachment.m_viewDescriptor.m_aspectFlags & aspects) == aspects;
        }

        //! The first clearing access of a transient image gives the resource its clear value.
        void GiveClearValue(RHIContext& context, const ImagePassAttachment& attachment)
        {
            if (attachment.m_action.m_loadAction == RHI::AttachmentLoadAction::Clear
                && context.Has<TransientTag>(attachment.m_image) && !context.Has<RHI::ClearValue>(attachment.m_image))
            {
                context.Add<RHI::ClearValue>(attachment.m_image, attachment.m_action.m_clearValue);
            }
        }
    }

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
        m_currentPass = pass;
    }

    void RenderGraphBuilder::EndPass()
    {
        ASSERT(m_currentPass != NullPass, "EndPass called without an active pass scope.");

        auto& passContext = *PassExecuteContext::Current();
        ASSERT(m_openScope == NullHandle,
            "Pass {} ends with Scope #{} still open: Close() it.",
            passContext.Get<PassName>(m_currentPass).m_name.GetCStr(), m_passScopeCount - 1);

        if constexpr (s_buildValidation)
        {
            if (m_passScopeCount > 1)
            {
                const auto* caps = passContext.TryGet<PassCapabilities>(m_currentPass);
                ASSERT(caps == nullptr || caps->m_collectViews == nullptr,
                    "Pass {} renders a view and has {} Scopes; a pass that renders a view has one.",
                    passContext.Get<PassName>(m_currentPass).m_name.GetCStr(), m_passScopeCount);
            }
        }

        m_currentPass    = NullPass;
        m_passScopeCount = 0;
    }

    void RenderGraphBuilder::CheckScopeOpen(RHIHandle scope) const
    {
        ASSERT(scope != NullHandle && scope == m_openScope,
            "Pass {}: adding to a Scope that is not open. A pass declares one Scope at a time, "
            "between opening it and Close().",
            PassExecuteContext::Current()->Get<PassName>(m_currentPass).m_name.GetCStr());
    }

    void RenderGraphBuilder::CloseScope(RHIHandle scope)
    {
        CheckScopeOpen(scope);

        // Checked here, not as each thing is declared: .View() and .Stage() refine an
        // attachment afterwards, and root constants are set one by one.
        if constexpr (s_buildValidation)
        {
            auto& rhiContext  = *RHIExecuteContext::Current();
            auto& passContext = *PassExecuteContext::Current();
            const char*    passName   = passContext.Get<PassName>(m_currentPass).m_name.GetCStr();
            const uint32_t scopeIndex = rhiContext.Get<Scope>(scope).m_index;

            ASSERT(!m_scopeAttachments.empty(),
                "Pass {} opened Scope #{} but declared no attachment in it.", passName, scopeIndex);

            // An unset root constant reads as the zeroed block's 0 — a heap index 0 or view
            // slot 0 that is silently wrong — so every field must be set in every Scope.
            const auto* layout = passContext.TryGet<PassPipelineLayout>(m_currentPass);
            const RHI::ConstantsLayout* rootConstants = (layout != nullptr && layout->m_layout)
                ? layout->m_layout->GetRootConstantsLayout() : nullptr;
            const auto* block = rhiContext.TryGet<ScopeRootConstants>(scope);
            if (rootConstants != nullptr && block != nullptr)
            {
                const auto fields = rootConstants->GetShaderInputList();
                for (uint32_t i = 0; i < static_cast<uint32_t>(fields.size()); ++i)
                {
                    const Interval interval = rootConstants->GetInterval(i);
                    for (uint32_t dword = interval.m_min / 4; dword < (interval.m_max + 3) / 4; ++dword)
                    {
                        ASSERT((block->m_writtenDwords & (1u << dword)) != 0,
                            "Pass {} Scope #{} never sets root constant {}.",
                            passName, scopeIndex, fields[i].m_name.GetCStr());
                    }
                }
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_scopeAttachments.size()); ++i)
            {
                const RHIHandle attachment = m_scopeAttachments[i];
                const auto*     image      = rhiContext.TryGet<ImagePassAttachment>(attachment);
                const auto*     buffer     = rhiContext.TryGet<BufferPassAttachment>(attachment);
                const char*     name       = image
                    ? image->m_attachmentId.m_id.GetCStr() : buffer->m_attachmentId.m_id.GetCStr();

                ASSERT((image ? image->m_stage : buffer->m_stage) != RHI::AttachmentStage::Uninitialized,
                    "Pass {} Scope #{}: the shader access of {} has no stage. Give it one with .Stage(...).",
                    passName, scopeIndex, name);

                // One whose resource is declared later is checked when End links the two.
                ASSERT(image == nullptr || image->m_image == NullHandle || CoversDepthStencilAspects(rhiContext, *image),
                    "Pass {} Scope #{}: the depth-stencil attachment of {} must cover every aspect of the image.",
                    passName, scopeIndex, name);

                // An image's default view is the whole image; a buffer has no default, its
                // elements' size and count are not something the buffer itself knows.
                ASSERT(buffer == nullptr || !rhiContext.Has<ShaderInputBinding>(attachment)
                        || buffer->m_viewDescriptor.m_elementCount != 0,
                    "Pass {} Scope #{}: the access of buffer {} is bound to a shader input but has no view. "
                    "Give it one with .View(...).",
                    passName, scopeIndex, name);

                for (uint32_t j = i + 1; j < static_cast<uint32_t>(m_scopeAttachments.size()); ++j)
                {
                    ASSERT(!AreOfOneResource(rhiContext, attachment, m_scopeAttachments[j])
                            || !AccessesConflict(rhiContext, attachment, m_scopeAttachments[j]),
                        "Pass {} Scope #{}: {} is declared more than once with conflicting accesses to the "
                        "same subresource.",
                        passName, scopeIndex, name);
                }
            }
        }

        m_openScope = NullHandle;
        m_scopeAttachments.clear();
    }

    RHIHandle RenderGraphBuilder::OpenScope()
    {
        ASSERT(m_currentPass != NullPass, "A Scope can only be opened inside a pass.");
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();
        ASSERT(m_openScope == NullHandle,
            "Pass {} opens a Scope while Scope #{} is still open: Close() it first.",
            passContext.Get<PassName>(m_currentPass).m_name.GetCStr(), m_passScopeCount - 1);

        const RHIHandle scope = rhiContext.CreateEntity();
        rhiContext.Add<Scope>(scope, Scope{
            m_currentPass,
            m_passScopeCount++,
            passContext.Get<PassExecuteQueue>(m_currentPass).m_queue });
        m_openScope = scope;
        m_scopeAttachments.clear();

        const auto* layout = passContext.TryGet<PassPipelineLayout>(m_currentPass);
        const RHI::ConstantsLayout* rootConstants = (layout != nullptr && layout->m_layout)
            ? layout->m_layout->GetRootConstantsLayout() : nullptr;
        if (rootConstants != nullptr)
        {
            auto& block = rhiContext.Add<ScopeRootConstants>(scope);
            block.m_byteCount = rootConstants->GetDataSize();

            // A pass that renders views has the executer write viewIndex at each view handle,
            // so it is marked set here: a .Constant("viewIndex") would be overwritten.
            const auto* caps = passContext.TryGet<PassCapabilities>(m_currentPass);
            const RHI::ShaderInputIndex viewIndex = rootConstants->FindShaderInputIndex(RHI::InputName("viewIndex"));
            if (caps != nullptr && caps->m_collectViews != nullptr && viewIndex != RHI::InvalidShaderInputIndex)
            {
                const Interval interval = rootConstants->GetInterval(viewIndex);
                ASSERT(interval.m_max - interval.m_min == 4, "Root constant viewIndex is not a 4-byte index.");
                block.m_viewIndexOffset = interval.m_min;
                block.m_writtenDwords  |= 1u << (interval.m_min / 4);
            }
        }
        return scope;
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
        m_resources.emplace(name, CreateTransientImageResource(name, desc, nullptr));
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
        m_resources.emplace(name, CreateTransientBufferResource(name, desc));
    }

    RHIHandle RenderGraphBuilder::FindResource(const RHI::AttachmentId& name) const
    {
        const auto it = m_resources.find(name);
        return it != m_resources.end() ? it->second : NullHandle;
    }

    RHIHandle RenderGraphBuilder::AddScopeAttachment(
        RHIHandle scope, uint32_t* colorCount, const RHI::AttachmentId& name,
        RHI::AttachmentUsage usage, RHI::AttachmentAccess access, RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction* action)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        // NullHandle while nothing has declared the name: the pass that does may come later.
        const RHIHandle resource = FindResource(name);
        ASSERT(resource == NullHandle || !IsBufferResource(rhiContext, resource),
            "{} is a buffer: a Scope accesses it with ReadBuffer / ReadWriteBuffer / WriteBuffer.", name.GetCStr());

        ImagePassAttachment a;
        a.m_attachmentId = AttachmentId{ name, 0 };
        a.m_access       = access;
        a.m_usage        = usage;
        a.m_stage        = stage;
        a.m_image        = resource;
        a.m_pass         = m_currentPass;
        if (action != nullptr)
        {
            a.m_action = *action;
        }

        const RHIHandle attachment = AddImageAttachment(a, scope, colorCount);
        if (resource != NullHandle)
        {
            GiveClearValue(rhiContext, a);
        }
        else
        {
            rhiContext.Add<UnlinkedAttachmentTag>(attachment);
        }
        return attachment;
    }

    RHIHandle RenderGraphBuilder::AddScopeBufferAttachment(
        RHIHandle scope, const RHI::AttachmentId& name,
        RHI::AttachmentUsage usage, RHI::AttachmentAccess access, RHI::AttachmentStage stage)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        const RHIHandle resource = FindResource(name);
        ASSERT(resource == NullHandle || IsBufferResource(rhiContext, resource),
            "{} is an image: a Scope accesses it with ReadImage / ReadWriteImage / WriteImage.", name.GetCStr());

        BufferPassAttachment a;
        a.m_attachmentId = AttachmentId{ name, 0 };
        a.m_access       = access;
        a.m_usage        = usage;
        a.m_stage        = stage;
        a.m_buffer       = resource;
        a.m_pass         = m_currentPass;

        const RHIHandle attachment = AddBufferAttachment(a, scope);
        if (resource == NullHandle)
        {
            rhiContext.Add<UnlinkedAttachmentTag>(attachment);
        }
        return attachment;
    }

    RHIHandle RenderGraphBuilder::AddImageAttachment(
        const ImagePassAttachment& attachment, RHIHandle scope, uint32_t* colorCount)
    {
        CheckScopeOpen(scope);

        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(handle, attachment);
        rhiContext.Add<ScopeAttachment>(handle, ScopeAttachment{ scope, attachment.m_image });
        if (attachment.m_usage == RHI::AttachmentUsage::RenderTarget)
        {
            ASSERT(colorCount != nullptr, "A render target outside a render pass Scope.");
            rhiContext.Add<ColorAttachmentIndex>(handle, ColorAttachmentIndex{ (*colorCount)++ });
        }
        m_scopeAttachments.push_back(handle);
        m_attachments.push_back(handle);
        return handle;
    }

    RHIHandle RenderGraphBuilder::AddBufferAttachment(const BufferPassAttachment& attachment, RHIHandle scope)
    {
        CheckScopeOpen(scope);

        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(handle, attachment);
        rhiContext.Add<ScopeAttachment>(handle, ScopeAttachment{ scope, attachment.m_buffer });
        m_scopeAttachments.push_back(handle);
        m_attachments.push_back(handle);
        return handle;
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

        const auto [it, inserted] = m_resources.emplace(name, resource);
        ASSERT(inserted || it->second == resource,
            "{} is already declared for another resource.", name.GetCStr());
    }

    RHIHandle RenderGraphBuilder::AddPreviousFrameAttachment(ImagePassAttachment attachment, RHIHandle scope)
    {
        // Frame offset 1 makes it a resource of its own to ResolveGraph, with readers and no
        // writer, so no edge. Which image it reads is End's to fill.
        attachment.m_attachmentId = AttachmentId{ attachment.m_attachmentId.m_id, 0, 1 };
        attachment.m_access       = RHI::AttachmentAccess::Read;
        attachment.m_pass         = m_currentPass;
        attachment.m_image        = NullHandle;

        const RHIHandle handle = AddImageAttachment(attachment, scope, nullptr);
        RHIExecuteContext::Current()->Add<PreviousFrameTag>(handle);
        return handle;
    }

    void RenderGraphBuilder::BindPreviousFrameValid(RHIHandle attachment, const RHI::InputName& input)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<PreviousFrameTag>(attachment),
            "BindValid({}) on an access that is not a ReadPreviousImage.", input.GetCStr());
        ASSERT(!rhiContext.Has<PreviousFrameValidBinding>(attachment),
            "The previous-frame read bound to {} already has a constant that takes its validity.", input.GetCStr());

        ReserveUintConstant(rhiContext.Get<ScopeAttachment>(attachment).m_scope, input);
        rhiContext.Add<PreviousFrameValidBinding>(attachment, PreviousFrameValidBinding{ input });
    }

    RHIHandle RenderGraphBuilder::AddScopeItem(RHIHandle scope)
    {
        CheckScopeOpen(scope);

        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle item = rhiContext.CreateEntity();
        rhiContext.Add<ScopeItem>(item, ScopeItem{ scope });
        return item;
    }

    void RenderGraphBuilder::AddScopeDispatch(
        RHIHandle scope, uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ)
    {
        auto& passContext = *PassExecuteContext::Current();
        const auto* groupSize = passContext.TryGet<PassThreadGroupSize>(m_currentPass);
        ASSERT(groupSize != nullptr, "Pass {} dispatches without a compute shader.",
            passContext.Get<PassName>(m_currentPass).m_name.GetCStr());

        RHI::DispatchItem item;
        item.m_arguments = RHI::DispatchArguments(RHI::DispatchDirect(
            threadCountX, threadCountY, threadCountZ, groupSize->m_x, groupSize->m_y, groupSize->m_z));
        RHIExecuteContext::Current()->Add<RHI::DispatchItem>(AddScopeItem(scope), item);
    }

    void RenderGraphBuilder::AddScopeDispatchIndirect(RHIHandle scope, RHIHandle arguments, uint64_t byteOffset)
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();
        ASSERT(passContext.Has<PassThreadGroupSize>(m_currentPass), "Pass {} dispatches without a compute shader.",
            passContext.Get<PassName>(m_currentPass).m_name.GetCStr());

        const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(arguments);
        ASSERT(buffer != nullptr && buffer->m_usage == RHI::AttachmentUsage::Indirect
                && rhiContext.Get<ScopeAttachment>(arguments).m_scope == scope,
            "A DispatchIndirect's arguments must be an IndirectArguments access of the same Scope.");

        // The buffer is filled in by lowering: it has no backing yet.
        RHI::DispatchItem item;
        item.m_arguments = RHI::DispatchArguments(RHI::DispatchIndirect(nullptr, byteOffset));

        const RHIHandle handle = AddScopeItem(scope);
        rhiContext.Add<RHI::DispatchItem>(handle, item);
        rhiContext.Add<ItemIndirectArguments>(handle, ItemIndirectArguments{ arguments });
    }

    void RenderGraphBuilder::AddScopeSelection(RHIHandle scope, ScopeSelections::Collect collect)
    {
        CheckScopeOpen(scope);

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

        //! Mark `interval` of the Scope's root constants set: each field is set once.
        void MarkRootConstantWritten(ScopeRootConstants& block, const Interval& interval, const RHI::InputName& input)
        {
            for (uint32_t dword = interval.m_min / 4; dword < (interval.m_max + 3) / 4; ++dword)
            {
                const uint32_t bit = 1u << dword;
                ASSERT((block.m_writtenDwords & bit) == 0, "Root constant {} is set twice in one Scope.", input.GetCStr());
                block.m_writtenDwords |= bit;
            }
        }
    }

    void RenderGraphBuilder::ReserveUintConstant(RHIHandle scope, const RHI::InputName& input)
    {
        CheckScopeOpen(scope);

        const RHI::PipelineLayoutDescriptor& layout = CurrentPassLayout();
        if (const RHI::ConstantsLayout* root = layout.GetRootConstantsLayout())
        {
            const RHI::ShaderInputIndex index = root->FindShaderInputIndex(input);
            if (index != RHI::InvalidShaderInputIndex)
            {
                const Interval interval = root->GetInterval(index);
                ASSERT(interval.m_max - interval.m_min == 4, "Root constant {} is not a 4-byte uint.", input.GetCStr());
                MarkRootConstantWritten(RHIExecuteContext::Current()->Get<ScopeRootConstants>(scope), interval, input);
                return;
            }
        }

        const RHI::ShaderInputConstantDescriptor* desc = layout.FindConstantDescriptor(input);
        ASSERT(desc != nullptr, "The pass's shaders have no constant {}.", input.GetCStr());
        ASSERT(desc->m_spaceId == kPerPassSpaceId,
            "{} is in space {}; only per-Scope (space {}) and per-pass (space {}) constants can be set from a Scope.",
            input.GetCStr(), desc->m_spaceId, kPerScopeSpaceId, kPerPassSpaceId);
        ASSERT(desc->m_constantByteCount == 4, "Constant {} is not a 4-byte uint.", input.GetCStr());
    }

    void RenderGraphBuilder::BindShaderInputIndex(RHIHandle attachment, const RHI::InputName& input)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<ImagePassAttachment>(attachment),
            "Binding {} by index to a buffer: buffer bindings are not supported yet.", input.GetCStr());
        ASSERT(!rhiContext.Has<ShaderInputBinding>(attachment) && !rhiContext.Has<IndexBinding>(attachment),
            "The access bound to {} is already bound; declare another access to bind another input.",
            input.GetCStr());

        ReserveUintConstant(rhiContext.Get<ScopeAttachment>(attachment).m_scope, input);
        rhiContext.Add<IndexBinding>(attachment, IndexBinding{ input });
    }

    void RenderGraphBuilder::BindShaderInput(RHIHandle attachment, const RHI::InputName& input)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        CheckScopeOpen(rhiContext.Get<ScopeAttachment>(attachment).m_scope);
        auto* image  = rhiContext.TryGet<ImagePassAttachment>(attachment);
        auto* buffer = image != nullptr ? nullptr : &rhiContext.Get<BufferPassAttachment>(attachment);
        ASSERT(!rhiContext.Has<ShaderInputBinding>(attachment) && !rhiContext.Has<IndexBinding>(attachment),
            "The access of {} is already bound; declare another access to bind another input.",
            image != nullptr ? image->m_attachmentId.m_id.GetCStr() : buffer->m_attachmentId.m_id.GetCStr());

        // What the shader says of the input: where it is, whether it writes, in which stages.
        uint32_t             spaceId       = 0;
        bool                 shaderWrites  = false;
        RHI::ShaderStageMask stageMask     = RHI::ShaderStageMask::None;
        if (image != nullptr)
        {
            const RHI::ShaderInputImageDescriptor* desc = CurrentPassLayout().FindImageDescriptor(input);
            ASSERT(desc != nullptr, "The pass's shaders have no image input {}.", input.GetCStr());
            spaceId      = desc->m_spaceId;
            shaderWrites = desc->m_access == RHI::ShaderInputImageAccess::ReadWrite;
            stageMask    = desc->m_stageMask;
        }
        else
        {
            const RHI::ShaderInputBufferDescriptor* desc = CurrentPassLayout().FindBufferDescriptor(input);
            ASSERT(desc != nullptr, "The pass's shaders have no buffer input {}.", input.GetCStr());
            spaceId      = desc->m_spaceId;
            shaderWrites = desc->m_access == RHI::ShaderInputBufferAccess::ReadWrite;
            stageMask    = desc->m_stageMask;
        }

        ASSERT(spaceId == kPerPassSpaceId,
            "{} is in space {}; only per-pass inputs (space {}) can be bound from a Scope.",
            input.GetCStr(), spaceId, kPerPassSpaceId);

        const RHI::AttachmentAccess access = image != nullptr ? image->m_access : buffer->m_access;
        const bool writes = CheckBitsAny(access, RHI::AttachmentAccess::Write);
        ASSERT(writes == shaderWrites,
            "{} is {} in the shader but the access {} it.",
            input.GetCStr(), writes ? "read-only" : "read-write", writes ? "writes" : "only reads");

        const RHI::AttachmentStage stage    = ToAttachmentStage(stageMask);
        RHI::AttachmentStage&      declared = image != nullptr ? image->m_stage : buffer->m_stage;
        if (declared == RHI::AttachmentStage::Uninitialized)
        {
            declared = stage;
        }
        else
        {
            ASSERT(declared == stage,
                "The stage declared for {} differs from the stages its shaders use it in.", input.GetCStr());
        }

        rhiContext.Add<ShaderInputBinding>(attachment, ShaderInputBinding{ input });
    }

    void RenderGraphBuilder::AddScopeSampler(RHIHandle scope, const RHI::InputName& input, const RHI::SamplerState& state)
    {
        CheckScopeOpen(scope);

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
        CheckScopeOpen(scope);

        if (const RHI::ConstantsLayout* root = CurrentPassLayout().GetRootConstantsLayout())
        {
            const RHI::ShaderInputIndex index = root->FindShaderInputIndex(input);
            if (index != RHI::InvalidShaderInputIndex)
            {
                const Interval interval = root->GetInterval(index);
                ASSERT(byteCount == interval.m_max - interval.m_min,
                    "Root constant {} takes {} bytes, given {}.", input.GetCStr(), interval.m_max - interval.m_min, byteCount);
                auto& block = RHIExecuteContext::Current()->Get<ScopeRootConstants>(scope);
                MarkRootConstantWritten(block, interval, input);
                memcpy(block.m_bytes.data() + interval.m_min, bytes, byteCount);
                return;
            }
        }

        const RHI::ShaderInputConstantDescriptor* desc = CurrentPassLayout().FindConstantDescriptor(input);
        ASSERT(desc != nullptr, "The pass's shaders have no constant {}.", input.GetCStr());
        ASSERT(desc->m_spaceId == kPerPassSpaceId,
            "{} is in space {}; only per-Scope (space {}) and per-pass (space {}) constants can be set from a Scope.",
            input.GetCStr(), desc->m_spaceId, kPerScopeSpaceId, kPerPassSpaceId);
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

    void RenderGraphBuilder::LinkAttachments()
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        for (auto [attachment, link] : rhiContext.GetView<UnlinkedAttachmentTag, ScopeAttachment>().each())
        {
            auto*                    image    = rhiContext.TryGet<ImagePassAttachment>(attachment);
            auto*                    buffer   = rhiContext.TryGet<BufferPassAttachment>(attachment);
            const RHI::AttachmentId& name     = image ? image->m_attachmentId.m_id : buffer->m_attachmentId.m_id;
            const Scope&             scope    = rhiContext.Get<Scope>(link.m_scope);
            const char*              passName = passContext.Get<PassName>(scope.m_pass).m_name.GetCStr();

            const RHIHandle resource = FindResource(name);
            ASSERT(resource != NullHandle,
                "Pass {} Scope #{} accesses {}, which no pass declares (Create / Import) this frame.",
                passName, scope.m_index, name.GetCStr());
            if (resource == NullHandle)
            {
                continue;
            }

            // What AddScopeAttachment / AddScopeBufferAttachment and CloseScope do for an
            // attachment that has its resource.
            const bool isBuffer = IsBufferResource(rhiContext, resource);
            ASSERT(isBuffer == (buffer != nullptr),
                "Pass {} Scope #{} accesses {} as {}, and it is declared as {}.",
                passName, scope.m_index, name.GetCStr(),
                buffer ? "a buffer" : "an image", isBuffer ? "a buffer" : "an image");
            if (isBuffer != (buffer != nullptr))
            {
                continue;
            }

            link.m_resource = resource;
            if (buffer != nullptr)
            {
                buffer->m_buffer = resource;
                continue;
            }

            image->m_image = resource;
            GiveClearValue(rhiContext, *image);
            ASSERT(CoversDepthStencilAspects(rhiContext, *image),
                "Pass {} Scope #{}: the depth-stencil attachment of {} must cover every aspect of the image.",
                passName, scope.m_index, name.GetCStr());
        }
        rhiContext.Clear<UnlinkedAttachmentTag>();
    }

    void RenderGraphBuilder::LinkPreviousFrameReads()
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        for (auto [attachment, image, link] :
             rhiContext.GetView<PreviousFrameTag, ImagePassAttachment, ScopeAttachment>().each())
        {
            const RHI::AttachmentId name     = image.m_attachmentId.m_id;
            const RHIHandle         declared = FindTransientImage(name);
            ASSERT(declared != NullHandle,
                "Pass {} reads last frame's {}, which no pass creates as a transient image this frame: a reader of the "
                "previous frame runs only while the producer does.",
                passContext.Get<PassName>(image.m_pass).m_name.GetCStr(), name.GetCStr());
            if (declared == NullHandle)
            {
                continue;
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

            image.m_image   = previous;
            link.m_resource = previous;
            if (missing)
            {
                rhiContext.Add<PreviousFrameMissingTag>(attachment);
            }
        }
    }

    void RenderGraphBuilder::CheckResolution() const
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        for (const GraphError& error : m_resolution.m_errors)
        {
            const Scope& scope    = rhiContext.Get<Scope>(rhiContext.Get<ScopeAttachment>(error.m_attachment).m_scope);
            const char*  passName = passContext.Get<PassName>(scope.m_pass).m_name.GetCStr();
            const char*  name     = GetAttachmentId(rhiContext, error.m_attachment).m_id.GetCStr();
            switch (error.m_type)
            {
            case GraphErrorType::ReadsUndefined:
                ASSERT(false,
                    "Pass {} Scope #{} reads {}, a transient resource nothing has written by then: no pass writes it "
                    "this frame, or only this pass in a later Scope.",
                    passName, scope.m_index, name);
                break;
            case GraphErrorType::FromPassWritesNothing:
                ASSERT(false,
                    "Pass {} Scope #{} takes {} .From({}), which does not write it this frame: the two run on the same "
                    "conditions, or the access drops its .From when that pass is off.",
                    passName, scope.m_index, name,
                    passContext.Get<PassName>(rhiContext.Get<FromPass>(error.m_attachment).m_pass).m_name.GetCStr());
                break;
            }
        }

        if (!m_resolution.m_unordered.empty())
        {
            eastl::string names;
            for (const Pass pass : m_resolution.m_unordered)
            {
                names += names.empty() ? "" : ", ";
                names += passContext.Get<PassName>(pass).m_name.GetCStr();
            }
            ASSERT(false,
                "[RenderGraphBuilder] The accesses of these passes need each other's results in a cycle, or wait for "
                "such passes: {}.",
                names.c_str());
        }
    }

    void RenderGraphBuilder::End()
    {
        ASSERT(m_currentPass == NullPass,
            "End() called with an active pass scope; missing EndPass?");

        LinkAttachments();
        LinkPreviousFrameReads();
        ResolveGraph(
            *RHIExecuteContext::Current(), *PassExecuteContext::Current(),
            eastl::span<const RHIHandle>(m_attachments.data(), m_attachments.size()), m_resolution);
        CheckResolution();

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
        m_attachments.clear();
        m_resources.clear();
        m_currentPass = NullPass;
    }
}
