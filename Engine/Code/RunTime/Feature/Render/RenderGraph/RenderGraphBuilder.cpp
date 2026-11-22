#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PassScopes.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Pass/PassContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/PassDrawListComponents.h>
#include <Pass/PassCapabilities.h>
#include <Binding/ShaderUsage.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Command/AccelerationStructureBuildItem.h>
#include <RHI/Command/DispatchItem.h>
#include <RHI/Command/DrawItem.h>

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
            const auto& lhsBuffer = context.Get<BufferPassAttachment>(lhs);
            const auto& rhsBuffer = context.Get<BufferPassAttachment>(rhs);
            if (lhsBuffer.m_stage == RHI::AttachmentStage::AccelerationStructureBuild
                && rhsBuffer.m_stage == RHI::AttachmentStage::AccelerationStructureBuild
                && lhsBuffer.m_usage == rhsBuffer.m_usage
                && (lhsBuffer.m_usage == RHI::AttachmentUsage::RayTracingAccelerationStructure
                    || lhsBuffer.m_usage == RHI::AttachmentUsage::RayTracingScratchBuffer))
            {
                // Builds in one Scope use disjoint ranges, currently guaranteed by the caller.
                return false;
            }
            return RHI::HasAccessConflict(ConvertAttachmentAccess(lhsBuffer), ConvertAttachmentAccess(rhsBuffer));
        }

        bool IsBufferResource(const RHIContext& context, RHIHandle resource)
        {
            return context.Has<RHI::BufferDescriptor>(resource) || context.Has<BackingBuffer>(resource);
        }

        bool IsBuildAccess(const RHIContext& context, RHIHandle scope, RHIHandle attachment,
            RHI::AttachmentUsage usage, RHI::AttachmentAccess access)
        {
            const auto* buffer = context.TryGet<BufferPassAttachment>(attachment);
            return buffer != nullptr && buffer->m_usage == usage && CheckBitsAll(buffer->m_access, access)
                && buffer->m_stage == RHI::AttachmentStage::AccelerationStructureBuild
                && context.Get<ScopeAttachment>(attachment).m_scope == scope;
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
        m_passImports.clear();
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

                // Buffer inputs, including AS ranges, require an explicit view.
                const bool direct = rhiContext.Has<ShaderInputBinding>(attachment)
                    || rhiContext.Has<SharedDirectAccessTag>(attachment);
                ASSERT(buffer == nullptr || !direct
                        || buffer->m_viewDescriptor.m_elementCount != 0,
                    "Pass {} Scope #{}: the access of buffer {} is bound to a shader input but has no view. "
                    "Give it one with .View(...).",
                    passName, scopeIndex, name);

                // Shared-binding reads only declare a buffer dependency.
                if (buffer != nullptr && buffer->m_usage == RHI::AttachmentUsage::RayTracingAccelerationStructure
                    && (CheckBitsAny(buffer->m_access, RHI::AttachmentAccess::Write)
                        || rhiContext.Has<ShaderInputBinding>(attachment)
                        || rhiContext.Has<AccelerationStructure>(attachment)))
                {
                    const auto* structure = rhiContext.TryGet<AccelerationStructure>(attachment);
                    ASSERT(structure != nullptr && structure->m_structure != nullptr
                            && structure->m_structure->IsInitialized(),
                        "Pass {} Scope #{}: AS attachment {} requires an initialized object via .AccelerationStructure(...).",
                        passName, scopeIndex, name);
                }

            }

            // Large sets mostly contain unrelated resources. Only compare accesses of one
            // resource, and separately retain name-based checks when either access is unlinked.
            using AccessList = eastl::fixed_vector<RHIHandle, 1>;
            eastl::unordered_map<RHIHandle, AccessList> byResource;
            bool hasUnlinked = false;
            for (RHIHandle attachment : m_scopeAttachments)
            {
                const RHIHandle resource = rhiContext.Get<ScopeAttachment>(attachment).m_resource;
                if (resource != NullHandle)
                {
                    byResource[resource].push_back(attachment);
                }
                else
                {
                    hasUnlinked = true;
                }
            }
            const auto checkGroup = [&](const AccessList& accesses, bool onlyUnlinked)
            {
                for (size_t i = 0; i < accesses.size(); ++i)
                {
                    for (size_t j = i + 1; j < accesses.size(); ++j)
                    {
                        if (onlyUnlinked
                            && rhiContext.Get<ScopeAttachment>(accesses[i]).m_resource != NullHandle
                            && rhiContext.Get<ScopeAttachment>(accesses[j]).m_resource != NullHandle)
                        {
                            continue;
                        }
                        ASSERT(!AreOfOneResource(rhiContext, accesses[i], accesses[j])
                                || !AccessesConflict(rhiContext, accesses[i], accesses[j]),
                            "Pass {} Scope #{}: {} is declared more than once with conflicting accesses to the "
                            "same subresource.", passName, scopeIndex, GetAttachmentId(rhiContext, accesses[i]).m_id.GetCStr());
                    }
                }
            };
            for (const auto& entry : byResource)
            {
                checkGroup(entry.second, false);
            }
            if (hasUnlinked)
            {
                eastl::unordered_map<RHI::AttachmentId, AccessList> byName;
                for (RHIHandle attachment : m_scopeAttachments)
                {
                    byName[GetAttachmentId(rhiContext, attachment).m_id].push_back(attachment);
                }
                for (const auto& entry : byName)
                {
                    checkGroup(entry.second, true);
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

        const auto* caps = passContext.TryGet<PassCapabilities>(m_currentPass);

        const auto* layout = passContext.TryGet<PassPipelineLayout>(m_currentPass);
        const RHI::ConstantsLayout* rootConstants = (layout != nullptr && layout->m_layout)
            ? layout->m_layout->GetRootConstantsLayout() : nullptr;
        if (rootConstants != nullptr)
        {
            auto& block = rhiContext.Add<ScopeRootConstants>(scope);
            block.m_byteCount = rootConstants->GetDataSize();

            // A pass that renders views has the executer write viewIndex at each view handle,
            // so it is marked set here: a .Constant("viewIndex") would be overwritten.
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
            ASSERT(m_resourceSets.find(name) == m_resourceSets.end(),
                "{} is already declared as a resource set.", name.GetCStr());
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
            ASSERT(m_resourceSets.find(name) == m_resourceSets.end(),
                "{} is already declared as a resource set.", name.GetCStr());
        }
        m_resources.emplace(name, CreateTransientBufferResource(name, desc));
    }

    RHIHandle RenderGraphBuilder::FindResource(const RHI::AttachmentId& name) const
    {
        ASSERT(m_resourceSets.find(name) == m_resourceSets.end(),
            "{} names a resource set; read its members with ReadSet.", name.GetCStr());
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
            "{} is not a buffer resource.", name.GetCStr());

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

    void RenderGraphBuilder::AddScopeImageSetReads(
        RHIHandle scope, const RHI::AttachmentId& name, RHI::AttachmentStage stage)
    {
        CheckScopeOpen(scope);
        const auto set = m_resourceSets.find(name);
        ASSERT(set != m_resourceSets.end(), "{} has not been imported as a shared Image set.", name.GetCStr());
        if (set == m_resourceSets.end())
        {
            return;
        }

        auto& context = *RHIExecuteContext::Current();
        for (RHIHandle resource : set->second)
        {
            const auto intent = m_passImports.find(resource);
            ASSERT(intent != m_passImports.end() && intent->second,
                "{}: each member must be imported as shared by the current pass before ReadSet.", name.GetCStr());
            ASSERT(context.Valid(resource) && !context.Has<DeadTag>(resource) && RHI::IsResourceReady(context, resource),
                "{}: its members must remain alive and ready during graph construction.", name.GetCStr());

            ImagePassAttachment attachment;
            const auto* resourceName = context.TryGet<ResourceName>(resource);
            attachment.m_attachmentId = AttachmentId{ resourceName != nullptr ? resourceName->m_name : name, 0 };
            attachment.m_access = RHI::AttachmentAccess::Read;
            attachment.m_usage = RHI::AttachmentUsage::Shader;
            attachment.m_stage = stage;
            attachment.m_image = resource;
            attachment.m_pass = m_currentPass;
            ApplySharedShaderUsage(AddImageAttachment(attachment, scope, nullptr));
        }
    }

    void RenderGraphBuilder::SetShaderAttachmentStage(RHIHandle attachment, RHI::AttachmentStage stage)
    {
        auto& context = *RHIExecuteContext::Current();
        CheckScopeOpen(context.Get<ScopeAttachment>(attachment).m_scope);
        auto* image = context.TryGet<ImagePassAttachment>(attachment);
        auto& declared = image != nullptr ? image->m_stage : context.Get<BufferPassAttachment>(attachment).m_stage;
        const bool direct = context.Has<ShaderInputBinding>(attachment)
            || context.Has<SharedDirectAccessTag>(attachment);
        ASSERT(!direct || declared == stage,
            "The requested stage differs from the stages of the shader input this access binds: a "
            "shared import takes its stage from the input, so it needs no .Stage(...).");
        if (!direct || declared == stage)
        {
            declared = stage;
        }
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
        ImportResourceInternal(name, resource, false);
    }

    void RenderGraphBuilder::ImportSharedResource(const RHI::AttachmentId& name, RHIHandle resource)
    {
        ImportResourceInternal(name, resource, true);
    }

    void RenderGraphBuilder::ImportSharedResourceSet(const RHI::AttachmentId& name, eastl::vector<RHIHandle> resources)
    {
        ASSERT(m_currentPass != NullPass, "BeginPass must be called before importing a resource set.");
        ASSERT(m_resources.find(name) == m_resources.end(),
            "{} is already declared as a single resource.", name.GetCStr());
        if (m_resources.find(name) != m_resources.end())
        {
            return;
        }

        auto& context = *RHIExecuteContext::Current();
        resources.erase(eastl::remove_if(resources.begin(), resources.end(), [&](RHIHandle resource)
        {
            const bool valid = context.Valid(resource) && !context.Has<DeadTag>(resource);
            ASSERT(valid, "{}: its resource set contains an invalid or dead entity.", name.GetCStr());
            if (!valid)
            {
                return true;
            }
            const bool image = context.Has<Image>(resource) || context.Has<ImagePerFrame>(resource);
            ASSERT(image, "{}: each resource set member must be an Image.", name.GetCStr());
            return !image || !RHI::IsResourceReady(context, resource);
        }), resources.end());
        eastl::sort(resources.begin(), resources.end());
        resources.erase(eastl::unique(resources.begin(), resources.end()), resources.end());

        auto set = m_resourceSets.find(name);
        if (set != m_resourceSets.end())
        {
            ASSERT(set->second == resources,
                "{} is already imported as a set with different members this frame.", name.GetCStr());
            if (set->second != resources)
            {
                return;
            }
        }
        else
        {
            set = m_resourceSets.emplace(name, eastl::move(resources)).first;
        }
        for (RHIHandle resource : set->second)
        {
            PrepareImportedResource(name, resource, true);
        }
    }

    void RenderGraphBuilder::ImportResourceInternal(const RHI::AttachmentId& name, RHIHandle resource, bool shared)
    {
        ASSERT(m_resourceSets.find(name) == m_resourceSets.end(),
            "{} is already declared as a resource set.", name.GetCStr());
        if (m_resourceSets.find(name) != m_resourceSets.end())
        {
            return;
        }
        PrepareImportedResource(name, resource, shared);

        const auto [it, inserted] = m_resources.emplace(name, resource);
        ASSERT(inserted || it->second == resource,
            "{} is already declared for another resource.", name.GetCStr());
    }

    void RenderGraphBuilder::PrepareImportedResource(const RHI::AttachmentId& name, RHIHandle resource, bool shared)
    {
        ASSERT(m_currentPass != NullPass, "BeginPass must be called before declaring resources.");
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Valid(resource), "Import of {}: the resource entity is invalid.", name.GetCStr());
        if (shared)
        {
            ASSERT(!rhiContext.Has<TransientTag>(resource),
                "Import of {} as shared: only a resource owned outside the graph publishes a usage.", name.GetCStr());
            const auto* image = rhiContext.TryGet<ShaderImageUsage>(resource);
            const auto* buffer = rhiContext.TryGet<ShaderBufferUsage>(resource);
            const auto* structure = rhiContext.TryGet<ShaderAccelerationStructureUsage>(resource);
            ASSERT(image || buffer || structure, "Import of {} as shared: no published shader usage.", name.GetCStr());
            const bool direct = (image && image->m_binding.m_mode == ShaderBindingMode::Direct)
                || (buffer && buffer->m_binding.m_mode == ShaderBindingMode::Direct)
                || (structure && structure->m_binding.m_mode == ShaderBindingMode::Direct);
            if (direct && !rhiContext.Has<SharedBindingImportTag>(resource))
            {
                rhiContext.Add<SharedBindingImportTag>(resource);
            }
        }

        if (!rhiContext.Has<ImportedTag>(resource))
        {
            rhiContext.Add<ImportedTag>(resource);
        }

        // Single-frame imports get their backing from the owning Image / Buffer component, once;
        // a swap chain image has it from its creation. Per-frame resources (ImagePerFrame) have
        // it refreshed every frame by RenderGraph::RefreshPerFrameBackings before Build. Views
        // come from the resource's view cache on demand.
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
        // A per-frame resource imported for the first time: RefreshPerFrameBackings ran before
        // Build and skipped it, not yet imported. This frame's copy, as it takes from now on.
        else if (auto* images = rhiContext.TryGet<ImagePerFrame>(resource))
        {
            if (!rhiContext.Has<BackingImage>(resource))
            {
                rhiContext.Add<BackingImage>(resource, BackingImage{ images->m_images[m_frameIndex].get() });
            }
        }
        else if (auto* buffers = rhiContext.TryGet<BufferPerFrame>(resource))
        {
            if (!rhiContext.Has<BackingBuffer>(resource))
            {
                rhiContext.Add<BackingBuffer>(resource, BackingBuffer{ buffers->m_buffers[m_frameIndex].get() });
            }
        }

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<BackingImage>(resource) || rhiContext.Has<BackingBuffer>(resource),
                "Imported resource {} has no backing. Single-frame: attach an Image / Buffer "
                "component before importing; per-frame: ensure RefreshPerFrameBackings ran.",
                name.GetCStr());
        }

        // The intent belongs to the resource, including when it is imported under several names.
        const auto [intent, isNew] = m_passImports.emplace(resource, shared);
        ASSERT(isNew || intent->second == shared,
            "{}: the pass imports this resource both as shared and not; one intent per resource.",
            name.GetCStr());
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
        if (!groupSize)
        {
            ASSERT(false, "Pass '{}' needs a reflected thread group size for a thread-count dispatch.",
                passContext.Get<PassName>(m_currentPass).m_name.GetCStr());
            return;
        }

        AddScopeDispatch(scope, RHI::DispatchDirect(
            threadCountX, threadCountY, threadCountZ, groupSize->m_x, groupSize->m_y, groupSize->m_z));
    }

    void RenderGraphBuilder::AddScopeDispatch(RHIHandle scope, const RHI::DispatchDirect& dispatch)
    {
        RHI::DispatchItem item;
        item.m_arguments = RHI::DispatchArguments(dispatch);
        RHIExecuteContext::Current()->Add<RHI::DispatchItem>(AddScopeItem(scope), item);
    }

    void RenderGraphBuilder::AddScopeDispatchIndirect(RHIHandle scope, RHIHandle arguments, uint64_t byteOffset)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
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

    void RenderGraphBuilder::AddScopeCopy(RHIHandle scope, RHIHandle source, uint64_t sourceOffset,
        RHIHandle destination, uint64_t destinationOffset, uint64_t byteCount)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        auto isCopyAccess = [&](RHIHandle access, bool writes)
        {
            const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(access);
            return buffer != nullptr && buffer->m_usage == RHI::AttachmentUsage::Copy
                && CheckBitsAny(buffer->m_access, RHI::AttachmentAccess::Write) == writes
                && rhiContext.Get<ScopeAttachment>(access).m_scope == scope;
        };
        ASSERT(isCopyAccess(source, false) && isCopyAccess(destination, true),
            "A copy's source and destination must be a CopySource and a CopyDestination of the same Scope.");

        // The buffers are filled in by lowering: they have no backing yet.
        RHI::CopyBufferDescriptor descriptor;
        descriptor.m_sourceOffset      = sourceOffset;
        descriptor.m_destinationOffset = destinationOffset;
        descriptor.m_size              = byteCount;

        const RHIHandle item = AddScopeItem(scope);
        rhiContext.Add<RHI::CopyItem>(item, RHI::CopyItem(descriptor));
        rhiContext.Add<ItemCopy>(item, ItemCopy{ source, destination });
    }

    void RenderGraphBuilder::AddScopeImageToBufferCopy(RHIHandle scope, RHIHandle source, RHIHandle destination)
    {
        auto& context = *RHIExecuteContext::Current();
        const auto* image = context.TryGet<ImagePassAttachment>(source);
        const auto* buffer = context.TryGet<BufferPassAttachment>(destination);
        const bool valid = image && image->m_usage == RHI::AttachmentUsage::Copy
            && image->m_access == RHI::AttachmentAccess::Read && context.Get<ScopeAttachment>(source).m_scope == scope
            && buffer && buffer->m_usage == RHI::AttachmentUsage::Copy
            && buffer->m_access == RHI::AttachmentAccess::Write && context.Get<ScopeAttachment>(destination).m_scope == scope;
        ASSERT(valid, "An image-to-buffer copy needs a CopyImageSource and a CopyDestination of the same Scope.");
        if (!valid)
        {
            return;
        }

        const RHIHandle item = AddScopeItem(scope);
        context.Add<RHI::CopyItem>(item, RHI::CopyItem(RHI::CopyImageToBufferDescriptor{}));
        context.Add<ItemCopy>(item, ItemCopy{ source, destination });
    }

    ItemAccelerationStructureBuild RenderGraphBuilder::MakeAccelerationStructureBuild(RHIHandle scope,
        RHIHandle destination, RHIHandle scratch) const
    {
        CheckScopeOpen(scope);
        const auto& context = *RHIExecuteContext::Current();
        ASSERT(IsBuildAccess(context, scope, destination,
                RHI::AttachmentUsage::RayTracingAccelerationStructure, RHI::AttachmentAccess::Write),
            "A build's storage must be an acceleration structure write access of the same Scope.");
        ASSERT(IsBuildAccess(context, scope, scratch,
                RHI::AttachmentUsage::RayTracingScratchBuffer, RHI::AttachmentAccess::ReadWrite),
            "A build's scratch must be a Scratch access of the same Scope.");

        ItemAccelerationStructureBuild build;
        build.m_destinationAttachment = destination;
        build.m_scratchAttachment = scratch;
        return build;
    }

    void RenderGraphBuilder::AddScopeBuildBottomLevel(RHIHandle scope, RHIHandle destination,
        eastl::span<const TriangleBuildInput> geometries, RHIHandle scratch,
        RHI::AccelerationStructureBuildFlags flags)
    {
        auto build = MakeAccelerationStructureBuild(scope, destination, scratch);
        ASSERT(!geometries.empty(), "A bottom level build requires geometries.");

        auto& context = *RHIExecuteContext::Current();
        ItemBottomLevelBuild inputs;
        inputs.m_geometries.reserve(geometries.size());
        inputs.m_geometryAttachments.reserve(geometries.size());
        for (const TriangleBuildInput& input : geometries)
        {
            const RHIHandle vertices = input.m_vertices.GetHandle();
            const RHIHandle indices = input.m_indices ? input.m_indices->GetHandle() : NullHandle;
            ASSERT(IsBuildAccess(context, scope, vertices,
                    RHI::AttachmentUsage::RayTracingBuildInput, RHI::AttachmentAccess::Read),
                "A build's vertices must be a BuildInput access of the same Scope.");
            ASSERT(indices == NullHandle || IsBuildAccess(context, scope, indices,
                    RHI::AttachmentUsage::RayTracingBuildInput, RHI::AttachmentAccess::Read),
                "A build's indices must be a BuildInput access of the same Scope.");
            RHI::RayTracingGeometry geometry;
            geometry.m_vertexFormat = input.m_vertexFormat;
            geometry.m_opaque = input.m_opaque;
            inputs.m_geometries.push_back(geometry);
            inputs.m_geometryAttachments.push_back(ItemBuildGeometry{vertices, indices});
        }

        RHI::AccelerationStructureBuildItem item;
        item.m_inputs.m_type = RHI::AccelerationStructureType::BottomLevel;
        item.m_inputs.m_flags = flags;

        const RHIHandle handle = AddScopeItem(scope);
        context.Add<ItemAccelerationStructureBuild>(handle, eastl::move(build));
        context.Add<ItemBottomLevelBuild>(handle, eastl::move(inputs));
        context.Add<RHI::AccelerationStructureBuildItem>(handle, item);
    }

    void RenderGraphBuilder::AddScopeBuildTopLevel(RHIHandle scope, RHIHandle destination, RHIHandle instances,
        RHIHandle scratch, RHI::AccelerationStructureBuildFlags flags)
    {
        auto build = MakeAccelerationStructureBuild(scope, destination, scratch);

        auto& context = *RHIExecuteContext::Current();
        ASSERT(IsBuildAccess(context, scope, instances,
                RHI::AttachmentUsage::RayTracingBuildInput, RHI::AttachmentAccess::Read),
            "A build's instances must be a BuildInput access of the same Scope.");
        RHI::AccelerationStructureBuildItem item;
        item.m_inputs.m_type = RHI::AccelerationStructureType::TopLevel;
        item.m_inputs.m_flags = flags;

        const RHIHandle handle = AddScopeItem(scope);
        context.Add<ItemAccelerationStructureBuild>(handle, eastl::move(build));
        context.Add<ItemTopLevelBuild>(handle, ItemTopLevelBuild{instances});
        context.Add<RHI::AccelerationStructureBuildItem>(handle, item);
    }

    void RenderGraphBuilder::AddScopeDrawIndirect(RHIHandle scope, RHIHandle arguments, RHIHandle count,
        eastl::span<const RHIHandle> vertices, RHIHandle indices, RHI::IndexFormat indexFormat)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        auto isIndirectAccess = [&](RHIHandle access)
        {
            const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(access);
            return buffer != nullptr && buffer->m_usage == RHI::AttachmentUsage::Indirect
                && rhiContext.Get<ScopeAttachment>(access).m_scope == scope;
        };
        ASSERT(isIndirectAccess(arguments) && (count == NullHandle || isIndirectAccess(count)),
            "A DrawIndirect's arguments and count must be IndirectArguments accesses of the same Scope.");
        // Without a count buffer a draw executes exactly as many records as it is told, which
        // only the groups of a list say.
        ASSERT(count != NullHandle || PassExecuteContext::Current()->Has<PassDrawList>(m_currentPass),
            "Pass {} draws indirect without a count but draws no list.",
            PassExecuteContext::Current()->Get<PassName>(m_currentPass).m_name.GetCStr());

        // The buffers and the record count are filled in by lowering: they have no backing yet.
        RHI::DrawItem item;
        item.m_drawArguments = RHI::DrawArguments(RHI::DrawIndexedIndirect(RHI::IndirectArguments{}));

        const RHIHandle handle = AddScopeItem(scope);
        rhiContext.Add<RHI::DrawItem>(handle, item);
        rhiContext.Add<ItemIndirectArguments>(handle, ItemIndirectArguments{ arguments, count });
        rhiContext.Add<ItemInputAssembly>(handle, MakeInputAssembly(scope, vertices, indices, indexFormat));
    }

    void RenderGraphBuilder::AddScopeDraw(RHIHandle scope, const RHI::DrawItem& draw,
        eastl::span<const RHIHandle> vertices, RHIHandle indices, RHI::IndexFormat indexFormat)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = AddScopeItem(scope);
        rhiContext.Add<RHI::DrawItem>(handle, draw);
        rhiContext.Add<ItemInputAssembly>(handle, MakeInputAssembly(scope, vertices, indices, indexFormat));
    }

    ItemInputAssembly RenderGraphBuilder::MakeInputAssembly(RHIHandle scope,
        eastl::span<const RHIHandle> vertices, RHIHandle indices, RHI::IndexFormat indexFormat) const
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        auto isInputAssemblyAccess = [&](RHIHandle access)
        {
            const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(access);
            return buffer != nullptr && buffer->m_usage == RHI::AttachmentUsage::InputAssembly
                && rhiContext.Get<ScopeAttachment>(access).m_scope == scope;
        };
        ASSERT(vertices.size() <= RHI::Limits::Pipeline::StreamCountMax,
            "A Draw reads {} vertex streams, more than the {} a pipeline takes.",
            vertices.size(), RHI::Limits::Pipeline::StreamCountMax);
        for (RHIHandle vertex : vertices)
        {
            ASSERT(isInputAssemblyAccess(vertex), "A Draw's vertex streams must be InputAssembly accesses of the same Scope.");
        }
        ASSERT(indices == NullHandle || isInputAssemblyAccess(indices),
            "A Draw's indices must be an InputAssembly access of the same Scope.");
        ASSERT(indices == NullHandle || indexFormat != RHI::IndexFormat::Unknown, "A Draw's indices need a format.");

        // The buffers are bound by lowering: they have no backing yet.
        ItemInputAssembly inputAssembly;
        inputAssembly.m_vertexAttachments.assign(vertices.begin(), vertices.end());
        inputAssembly.m_indexAttachment = indices;
        inputAssembly.m_indexFormat     = indexFormat;
        return inputAssembly;
    }

    const RHI::PipelineLayoutDescriptor& RenderGraphBuilder::CurrentPassLayout() const
    {
        auto& passContext = *PassExecuteContext::Current();
        const auto* layout = passContext.TryGet<PassPipelineLayout>(m_currentPass);
        ASSERT(layout != nullptr && layout->m_layout,
            "Pass {} binds shader inputs but has no pipeline layout.",
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

        const auto& layout = CurrentPassLayout();
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

        const auto* desc = layout.FindConstantDescriptor(input);
        ASSERT(desc != nullptr && desc->m_constantByteCount == 4,
            "Constant {} is not a 4-byte uint.", input.GetCStr());
    }

    void RenderGraphBuilder::BindShaderInputIndex(RHIHandle attachment, const RHI::InputName& input)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<ImagePassAttachment>(attachment),
            "Binding {} by index to a buffer: buffer bindings are not supported yet.", input.GetCStr());
        if (rhiContext.Has<ShaderInputBinding>(attachment) || rhiContext.Has<SharedDirectAccessTag>(attachment)
            || rhiContext.Has<IndexBinding>(attachment))
        {
            ASSERT(false, "The access bound to {} is already bound; declare another access to bind another input.",
                input.GetCStr());
            return;
        }

        ReserveUintConstant(rhiContext.Get<ScopeAttachment>(attachment).m_scope, input);
        rhiContext.Add<IndexBinding>(attachment, IndexBinding{ input });
    }

    void RenderGraphBuilder::SetIndirectArgumentsOfView(RHIHandle attachment, RHIHandle view, uint64_t byteOffset)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        CheckScopeOpen(rhiContext.Get<ScopeAttachment>(attachment).m_scope);

        const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(attachment);
        ASSERT(buffer != nullptr, ".IndirectArgumentsOf on an access of an image.");
        const char* name = buffer->m_attachmentId.m_id.GetCStr();
        ASSERT(CheckBitsAny(buffer->m_access, RHI::AttachmentAccess::Write),
            ".IndirectArgumentsOf on an access that only reads {}: it marks the write of a view's part.", name);

        // Marking a buffer no indirect call can read would do nothing, silently.
        const auto* descriptor = buffer->m_buffer != NullHandle
            ? rhiContext.TryGet<RHI::BufferDescriptor>(buffer->m_buffer) : nullptr;
        ASSERT(descriptor != nullptr && CheckBitsAll(descriptor->m_bindFlags, RHI::BufferBindFlags::Indirect),
            ".IndirectArgumentsOf on {}, which the graph has not created with BufferBindFlags::Indirect "
            "before this access.", name);

        ASSERT(view != NullHandle, ".IndirectArgumentsOf on {} names no view.", name);
        ASSERT(!rhiContext.Has<IndirectArgumentsOfView>(attachment),
            "The access of {} already writes the indirect arguments of a view.", name);
        rhiContext.Add<IndirectArgumentsOfView>(attachment, IndirectArgumentsOfView{ view, byteOffset });
    }

    bool RenderGraphBuilder::IsImportedAsShared(RHIHandle attachment) const
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        RHIHandle resource = NullHandle;
        if (const auto* image = rhiContext.TryGet<ImagePassAttachment>(attachment))
        {
            resource = image->m_image;
        }
        else if (const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(attachment))
        {
            resource = buffer->m_buffer;
        }
        if (resource == NullHandle)
        {
            return false;
        }

        const auto intent = m_passImports.find(resource);
        return intent != m_passImports.end() && intent->second;
    }

    void RenderGraphBuilder::ApplySharedShaderUsage(RHIHandle attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        auto* image  = rhiContext.TryGet<ImagePassAttachment>(attachment);
        auto* buffer = image != nullptr ? nullptr : rhiContext.TryGet<BufferPassAttachment>(attachment);
        ASSERT(image != nullptr || buffer != nullptr, "Applying the published usage of no attachment.");
        if (image == nullptr && buffer == nullptr)
        {
            return;
        }

        const RHIHandle resource = image != nullptr ? image->m_image : buffer->m_buffer;
        ASSERT(resource != NullHandle, "Applying a published usage before its resource is declared.");
        if (resource == NullHandle)
        {
            return;
        }

        const RHI::AttachmentId& name = image != nullptr ? image->m_attachmentId.m_id : buffer->m_attachmentId.m_id;

        if (image != nullptr)
        {
            const auto* usage = rhiContext.TryGet<ShaderImageUsage>(resource);
            ASSERT(usage != nullptr, "{}: its resource publishes no image usage.", name.GetCStr());
            if (usage == nullptr)
            {
                return;
            }
            image->m_viewDescriptor = usage->m_view;
            switch (usage->m_binding.m_mode)
            {
            case ShaderBindingMode::Direct:
                BindShaderInputInternal(attachment, usage->m_binding, true);
                break;
            case ShaderBindingMode::Bindless:
                // Its consumer supplies the index through its own data. This access only
                // declares the published view's resource dependency.
                break;
            default:
                ASSERT(false, "{}: its resource publishes an unknown shader binding mode.", name.GetCStr());
                break;
            }
            return;
        }

        if (const auto* usage = rhiContext.TryGet<ShaderAccelerationStructureUsage>(resource))
        {
            // The access needs the byte range for barriers. The resource owns the bound object.
            const auto* owned = rhiContext.TryGet<AccelerationStructure>(resource);
            const auto* structure = owned != nullptr ? owned->m_structure.get() : nullptr;
            if (structure == nullptr || !structure->IsInitialized())
            {
                ASSERT(false, "{}: its resource has no initialized acceleration structure.", name.GetCStr());
                return;
            }
            buffer->m_viewDescriptor = RHI::BufferViewDescriptor::CreateRaw(
                static_cast<uint32_t>(structure->GetOffset()), static_cast<uint32_t>(structure->GetSize()));
            BindShaderInputInternal(attachment, usage->m_binding, true);
            return;
        }

        const auto* usage = rhiContext.TryGet<ShaderBufferUsage>(resource);
        ASSERT(usage != nullptr, "{}: its resource publishes no buffer usage.", name.GetCStr());
        if (usage == nullptr)
        {
            return;
        }
        buffer->m_viewDescriptor = usage->m_view;
        BindShaderInputInternal(attachment, usage->m_binding, true);
    }

    void RenderGraphBuilder::BindShaderInput(RHIHandle attachment, const ShaderInputBinding& binding)
    {
        BindShaderInputInternal(attachment, binding, false);
    }

    void RenderGraphBuilder::BindShaderInputInternal(
        RHIHandle attachment, const ShaderInputBinding& binding, bool published)
    {
        ASSERT(binding.m_mode == ShaderBindingMode::Direct,
            "A fixed shader input requires Direct binding. Bindless publication is supported only for Images.");
        if (binding.m_mode != ShaderBindingMode::Direct)
        {
            return;
        }

        const auto& input = binding.m_input;
        auto& rhiContext = *RHIExecuteContext::Current();
        CheckScopeOpen(rhiContext.Get<ScopeAttachment>(attachment).m_scope);
        auto* image  = rhiContext.TryGet<ImagePassAttachment>(attachment);
        auto* buffer = image != nullptr ? nullptr : &rhiContext.Get<BufferPassAttachment>(attachment);
        if (rhiContext.Has<ShaderInputBinding>(attachment) || rhiContext.Has<SharedDirectAccessTag>(attachment)
            || rhiContext.Has<IndexBinding>(attachment))
        {
            ASSERT(false,
                "The access of {} is already bound. A resource imported with ImportShared binds where it publishes "
                "its usage and takes no .Bind; otherwise declare another access to bind another input.",
                image != nullptr ? image->m_attachmentId.m_id.GetCStr() : buffer->m_attachmentId.m_id.GetCStr());
            return;
        }

        const auto& layout = CurrentPassLayout();
        const auto* group = layout.FindSpaceGroupBySpaceId(binding.m_spaceId);
        if (group == nullptr)
        {
            ASSERT(false, "The pass's layout has no space {} for input {}.", binding.m_spaceId, input.GetCStr());
            return;
        }

        // Search within the requested space; a name in another group is not this input.
        bool                 found         = false;
        uint32_t             inputCount    = 0;
        bool                 shaderWrites  = false;
        RHI::ShaderStageMask stageMask     = RHI::ShaderStageMask::None;
        auto takeInput = [&](const auto& desc)
        {
            if (desc.m_name != input)
            {
                return false;
            }
            inputCount = desc.m_count;
            stageMask = desc.m_stageMask;
            return true;
        };
        const bool isStructure = buffer != nullptr
            && buffer->m_usage == RHI::AttachmentUsage::RayTracingAccelerationStructure;
        for (const auto& handle : group->m_shaderInputs)
        {
            if (image != nullptr && handle.m_type == RHI::ShaderInputType::Image)
            {
                const auto& desc = layout.GetImageDescriptor(handle.m_index);
                found = takeInput(desc);
                if (found)
                {
                    shaderWrites = desc.m_access == RHI::ShaderInputImageAccess::ReadWrite;
                }
            }
            else if (isStructure && handle.m_type == RHI::ShaderInputType::AccelerationStructure)
            {
                found = takeInput(layout.GetAccelerationStructureDescriptor(handle.m_index));
            }
            else if (buffer != nullptr && !isStructure && handle.m_type == RHI::ShaderInputType::Buffer)
            {
                const auto& desc = layout.GetBufferDescriptor(handle.m_index);
                found = takeInput(desc);
                if (found)
                {
                    shaderWrites = desc.m_access == RHI::ShaderInputBufferAccess::ReadWrite;
                }
            }
            if (found)
            {
                break;
            }
        }
        if (!found)
        {
            ASSERT(false, "Space {} has no input {} matching this resource type.", binding.m_spaceId, input.GetCStr());
            return;
        }

        if (binding.m_arrayIndex >= inputCount)
        {
            ASSERT(false, "Input {} in space {} has {} elements, but array index {} was requested.",
                input.GetCStr(), binding.m_spaceId, inputCount, binding.m_arrayIndex);
            return;
        }

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

        if (published)
        {
            rhiContext.Add<SharedDirectAccessTag>(attachment);
        }
        else
        {
            rhiContext.Add<ShaderInputBinding>(attachment, binding);
        }
    }

    void RenderGraphBuilder::AddScopeSampler(RHIHandle scope, const RHI::InputName& input, const RHI::SamplerState& state)
    {
        CheckScopeOpen(scope);

        if (CurrentPassLayout().FindSamplerDescriptor(input) == nullptr)
        {
            ASSERT(false, "The pass's layout has no sampler {}.", input.GetCStr());
            return;
        }

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
        RHIHandle scope, const RHI::InputName& input, const void* bytes, size_t byteCount)
    {
        CheckScopeOpen(scope);

        const auto& layout = CurrentPassLayout();
        if (const RHI::ConstantsLayout* root = layout.GetRootConstantsLayout())
        {
            const RHI::ShaderInputIndex index = root->FindShaderInputIndex(input);
            if (index != RHI::InvalidShaderInputIndex)
            {
                const Interval interval = root->GetInterval(index);
                const size_t expectedByteCount = interval.m_max - interval.m_min;
                if (byteCount != expectedByteCount)
                {
                    ASSERT(false, "Root constant {} takes {} bytes, given {}.", input.GetCStr(), expectedByteCount, byteCount);
                    return;
                }
                auto& block = RHIExecuteContext::Current()->Get<ScopeRootConstants>(scope);
                MarkRootConstantWritten(block, interval, input);
                memcpy(block.m_bytes.data() + interval.m_min, bytes, byteCount);
                return;
            }
        }

        const auto* desc = layout.FindConstantDescriptor(input);
        if (desc == nullptr)
        {
            ASSERT(false, "The pass's layout has no constant {}.", input.GetCStr());
            return;
        }
        const size_t expectedByteCount = static_cast<size_t>(desc->m_elementCount) * desc->m_elementByteSize;
        if (byteCount != expectedByteCount)
        {
            ASSERT(false, "Constant {} takes {} bytes, given {}.", input.GetCStr(), expectedByteCount, byteCount);
            return;
        }
        if (byteCount > ScopeConstant::ByteCountMax)
        {
            ASSERT(false, "Constant {} is {} bytes; a Scope constant holds at most {}.",
                input.GetCStr(), byteCount, ScopeConstant::ByteCountMax);
            return;
        }

        auto& rhiContext = *RHIExecuteContext::Current();
        auto*  component  = rhiContext.TryGet<ScopeConstants>(scope);
        auto& constants  = (component != nullptr ? *component : rhiContext.Add<ScopeConstants>(scope)).m_constants;
        for (const ScopeConstant& constant : constants)
        {
            ASSERT(constant.m_input != input, "Constant {} is set twice in one Scope.", input.GetCStr());
        }
        ScopeConstant constant;
        constant.m_input     = input;
        constant.m_byteCount = static_cast<uint32_t>(byteCount);
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
        m_resourceSets.clear();
        m_currentPass = NullPass;
    }
}
