#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/unordered_set.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <Math/Vector2.h>

#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/Component/ScopeComponents.h>
#include <Pass/PassContext.h>
#include <RHI/Context/RHIContext.h>

#include "PooledImage.h"

namespace Spark::RHI
{
    class ImagePool;
}

namespace Spark::Render
{
    //! Per-call binding info for a transient image attachment (Create / Read /
    //! Write / ReadWrite). Bundles "how this pass slot looks at the resource"
    //! and "how this pass uses the slot" so the builder API stays small.
    //!
    //! m_view defaults to a default-constructed ImageViewDescriptor, which
    //! CompileTransientResources interprets as "view onto the entire resource".
    //! For Read / Write / ReadWrite the access is implied by the function name
    //! and is set by the builder; CreateImageAttachment takes access as a
    //! separate parameter (defaulting to Write) since the producing pass may
    //! also want to read the freshly-created resource (RW).
    struct ImageAttachmentBindInfo
    {
        RHI::InputName                 m_slot;
        RHI::InputName                 m_resolveSourceSlot;
        RHI::ImageViewDescriptor       m_view  {};
        RHI::AttachmentUsage           m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage           m_stage  = RHI::AttachmentStage::Any;
        RHI::AttachmentLoadStoreAction m_action {};
    };

    //! Per-call binding info for an imported image attachment. The caller supplies
    //! the resource entity + a view descriptor; the view is resolved on demand from
    //! the resource's view cache. Access is part of this struct because the same
    //! imported resource may be consumed with different access in different passes.
    struct ImportedImageAttachmentBindInfo
    {
        RHI::InputName                 m_slot;
        RHI::InputName                 m_resolveSourceSlot;
        //! The imported image resource entity (e.g. GetCurrentSwapChainResource()),
        //! plus the view descriptor to resolve against it. The view comes from the
        //! resource's view cache (single-frame or per-frame), not a pre-built view entity.
        RHIHandle                      m_image {NullHandle};
        RHI::ImageViewDescriptor       m_viewDescriptor {};
        RHI::AttachmentAccess          m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage           m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage           m_stage  = RHI::AttachmentStage::Any;
        RHI::AttachmentLoadStoreAction m_action {};
    };

    struct BufferAttachmentBindInfo
    {
        RHI::InputName            m_slot;
        RHI::BufferViewDescriptor m_view  {};
        RHI::AttachmentUsage      m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage      m_stage  = RHI::AttachmentStage::Any;
    };

    struct ImportedBufferAttachmentBindInfo
    {
        RHI::InputName            m_slot;
        //! The imported buffer resource entity + the view descriptor to resolve
        //! against it (mirrors ImportedImageAttachmentBindInfo).
        RHIHandle                 m_buffer {NullHandle};
        RHI::BufferViewDescriptor m_viewDescriptor {};
        RHI::AttachmentAccess     m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage      m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage      m_stage  = RHI::AttachmentStage::Any;
    };

    class RenderGraphBuilder
    {
    public:
        RenderGraphBuilder() = default;
        ~RenderGraphBuilder() = default;

        // ============================================================
        // Import — caller already owns the resource and a view onto it.
        // ============================================================

        template<typename PassTag>
        void ImportImageAttachment(
            const RHI::AttachmentId& name,
            const ImportedImageAttachmentBindInfo& bind);

        template<typename PassTag>
        void ImportBufferAttachment(
            const RHI::AttachmentId& name,
            const ImportedBufferAttachmentBindInfo& bind);

        // ============================================================
        // Create — declare a transient resource and use it in this pass.
        // The resource entity is materialized in RHIContext immediately
        // (TransientTag + ResourceName + ImageDescriptor / BufferDescriptor);
        // its actual RHI::Image / RHI::Buffer and views are produced later
        // by RenderGraphCompiler::CompileTransientResources.
        // ============================================================

        template<typename PassTag>
        void CreateImageAttachment(
            const RHI::AttachmentId&        name,
            const RHI::ImageDescriptor&     desc,
            const ImageAttachmentBindInfo&  bind,
            RHI::AttachmentAccess           access = RHI::AttachmentAccess::Write);

        template<typename PassTag>
        void CreateBufferAttachment(
            const RHI::AttachmentId&        name,
            const RHI::BufferDescriptor&    desc,
            const BufferAttachmentBindInfo& bind,
            RHI::AttachmentAccess           access = RHI::AttachmentAccess::Write);

        // ============================================================
        // Read* / Write* / ReadWrite* — auto-version helpers. Refer to a
        // previously Create'd or Imported attachment by bare name; the
        // builder resolves the version. Write* injects an implicit Read on
        // the current latest version (modeling "consume current content"),
        // then bumps the version and registers the Write.
        // ReadWrite* shares Write*'s versioning but registers the
        // attachment with access = ReadWrite — a single-slot RMW.
        // ============================================================

        template<typename PassTag>
        AttachmentId ReadImageAttachment(
            const RHI::AttachmentId&        name,
            const ImageAttachmentBindInfo&  bind);

        template<typename PassTag>
        AttachmentId WriteImageAttachment(
            const RHI::AttachmentId&        name,
            const ImageAttachmentBindInfo&  bind);

        template<typename PassTag>
        AttachmentId ReadWriteImageAttachment(
            const RHI::AttachmentId&        name,
            const ImageAttachmentBindInfo&  bind);

        // ============================================================
        // ReadPrevious* — read the copy of an attachment produced one frame ago.
        // Imports a pooled image, a separate resource from this frame's same-named
        // one, so it imposes no ordering on this frame's producer. Also marks this
        // frame's resource for extraction, so the next frame can read it back.
        // The name must already be declared this frame, as for every Read*.
        // Execute checks IsPreviousFrameMissing before trusting the content.
        // ============================================================

        template<typename PassTag>
        AttachmentId ReadPreviousImageAttachment(
            const RHI::AttachmentId&        name,
            const ImageAttachmentBindInfo&  bind);

        template<typename PassTag>
        AttachmentId ReadBufferAttachment(
            const RHI::AttachmentId&        name,
            const BufferAttachmentBindInfo& bind);

        template<typename PassTag>
        AttachmentId WriteBufferAttachment(
            const RHI::AttachmentId&        name,
            const BufferAttachmentBindInfo& bind);

        template<typename PassTag>
        AttachmentId ReadWriteBufferAttachment(
            const RHI::AttachmentId&        name,
            const BufferAttachmentBindInfo& bind);

        uint32_t GetFrameIndex() const { return m_frameIndex; }

        //! Internal scene resolution for this frame, seeded by the driver via Begin: what
        //! every pass before the temporal upscaler sizes its targets against. Output size
        //! scaled by the screen percentage. Re-read every frame, so resize works without
        //! rebuilding the pass. NOTE: single view for now; becomes per-view-keyed when
        //! multi-view lands.
        Math::Vector2Int GetRenderSize() const { return m_renderSize; }

        //! Resolution the scene is finally displayed at (swap chain size for a fullscreen
        //! game, editor viewport panel size in-editor) — NOT necessarily the swap chain.
        //! The temporal upscaler's output and everything after it use this.
        Math::Vector2Int GetOutputSize() const { return m_outputSize; }

        RHI::RHIHandle GetCurrentSwapChainResource() const { return m_curSwapChainResource; }

    private:
        friend class RenderGraph;
        friend class RenderPassScopes;
        friend class RenderScope;
        friend class ComputePassScopes;
        friend class ComputeScope;
        friend class ShaderAttachment;

        void AddEdge(Pass from, Pass to);

        void TouchNode(Pass pass);

        void BuildGraph();

        eastl::vector<Pass> TopoSort();

        void Begin(uint32_t frameIndex, RHI::RHIHandle swapChainResource,
                   const Math::Vector2Int& renderSize, const Math::Vector2Int& outputSize);

        eastl::vector<Pass> End();

        void BeginPass(Pass pass);

        void EndPass();

        static constexpr bool s_buildValidation { true };

        static RHI::AttachmentAccess NormalizeImageAccess(
            RHI::AttachmentAccess access,
            const RHI::AttachmentLoadStoreAction& action)
        {
            if (action.m_loadAction == RHI::AttachmentLoadAction::Load)
            {
                access |= RHI::AttachmentAccess::Read;
            }
            return access;
        }

        //! Open a new Scope of the current pass, numbered after those it already has.
        RHIHandle OpenScope();

        //! The Scope the PassTag-templated API declares into: opened on its first attachment,
        //! so a pass declared through RenderPassScopes gets none it did not ask for.
        RHIHandle CurrentScope();

        //! Introduce a transient resource under `name`, with no access yet.
        void CreateImage(const RHI::AttachmentId& name, const RHI::ImageDescriptor& desc);
        void CreateBuffer(const RHI::AttachmentId& name, const RHI::BufferDescriptor& desc);

        //! Introduce `resource`, owned outside the graph, under `name`, with no access yet.
        //! Importing a name again is fine for the same resource only.
        void ImportResource(const RHI::AttachmentId& name, RHIHandle resource);

        //! Add to `scope` a read of the copy of `attachment`'s name produced last frame: a
        //! pooled image, a stand-in when there is none (PreviousFrameMissingTag). Also marks
        //! this frame's resource for extraction, so the next frame can read it back. The
        //! caller fills the attachment's id, usage, stage, action and view.
        RHIHandle AddPreviousFrameAttachment(ImagePassAttachment attachment, RHIHandle scope);

        //! Bind `attachment` to the shader input `input` of the current pass's per-pass space:
        //! checks the input exists there and suits the access (SRV for reads, UAV for
        //! writes), and gives the attachment the input's stages, or checks them against its
        //! own.
        void BindShaderInput(RHIHandle attachment, const RHI::InputName& input);

        //! A sampler / constant `scope` sets in the current pass's per-pass space.
        void AddScopeSampler(RHIHandle scope, const RHI::InputName& input, const RHI::SamplerState& state);
        void AddScopeConstant(RHIHandle scope, const RHI::InputName& input, const void* bytes, uint32_t byteCount);

        //! The current pass's pipeline layout, reflected from its shaders.
        const RHI::PipelineLayoutDescriptor& CurrentPassLayout() const;

        //! Add to `scope` an access of the resource called `name`: reads use its latest
        //! version; writes consume it (a graph-only read) and produce the next. Whether the
        //! attachment is an image or a buffer is the resource's. The first clearing access
        //! of a transient image gives the resource its clear value. `colorCount` numbers the
        //! Scope's render targets (null outside a render pass); `action` is for render
        //! targets and depth only.
        RHIHandle AddScopeAttachment(
            RHIHandle scope, uint32_t* colorCount, const RHI::AttachmentId& name,
            RHI::AttachmentUsage usage, RHI::AttachmentAccess access, RHI::AttachmentStage stage,
            const RHI::AttachmentLoadStoreAction* action);

        // Pure registration into `scope`: build attachment entity, attach components, record
        // use. No validation. The PassTag-templated forms also stamp PassTag, which the
        // slot-based lookups of the PassTag API find attachments by.
        RHIHandle AddImageAttachment(const ImagePassAttachment& attachment, RHIHandle scope, uint32_t* colorCount);
        RHIHandle AddBufferAttachment(const BufferPassAttachment& attachment, RHIHandle scope);

        void CountScopeAttachment(RHIHandle scope);

        // Takes the attachment by value: links the imported resource (m_image / m_buffer)
        // at declaration time when it was not set by the caller.
        template<typename PassTag>
        RHIHandle RegisterBufferAttachment(BufferPassAttachment attachment);

        template<typename PassTag>
        RHIHandle RegisterImageAttachment(ImagePassAttachment attachment);

        // The imported resource declared under `name`, or NullHandle when the name is
        // transient — then it is linked later by CompileTransientResources.
        RHIHandle FindImportedResource(const RHI::AttachmentId& name) const;

        // The transient image declared under `name`, or NullHandle. Used to link a
        // previous-frame read to the resource it mirrors.
        RHIHandle FindTransientImage(const RHI::AttachmentId& name) const;

        // The pooled image holding last frame's `name`, or NullHandle.
        static RHIHandle FindPreviousFrameImage(const RHI::AttachmentId& name);

        // Materialize a transient resource entity in RHIContext: TransientTag,
        // ResourceName, the resource descriptor, and the clear value it was declared
        // with — everything needed to create the resource, without consulting the
        // attachments that reference it.
        RHIHandle CreateTransientImageResource(
            const RHI::AttachmentId&    name,
            const RHI::ImageDescriptor& desc,
            const RHI::ClearValue*      clearValue);

        RHIHandle CreateTransientBufferResource(
            const RHI::AttachmentId&     name,
            const RHI::BufferDescriptor& desc);

        template<typename PassTag, typename ComponentT>
        static void ValidateUniqueSlot(const RHI::InputName& slot);

        // Version resolution helpers. Both assert that the name has been
        // created / imported.
        uint32_t LookupLatestVersion(const RHI::AttachmentId& name) const;
        uint32_t BumpVersion(const RHI::AttachmentId& name);

        struct AttachmentEntry
        {
            AttachmentEntry() = default;
            AttachmentEntry(Pass p, RHI::AttachmentAccess a)
                : pass(p), access(a)
            {}

            Pass pass;
            RHI::AttachmentAccess access;
        };

        struct PassNode
        {
            eastl::unordered_set<Pass> dependents;
            uint32_t inDegree = 0;
        };

        //! A Scope the current pass opened, for the checks at EndPass.
        struct OpenedScope
        {
            RHIHandle m_scope {NullHandle};
            uint32_t  m_attachmentCount {0};
        };

        //! What Create / Import put under a name: the resource, and the latest version of it
        //! produced so far (bumped by every write).
        struct ResourceEntry
        {
            RHIHandle m_resource {NullHandle};
            uint32_t  m_latestVersion {0};
        };

        Pass m_currentPass {NullPass};

        //! See CurrentScope.
        RHIHandle m_currentScope {NullHandle};

        //! RenderTarget attachments declared in m_currentScope so far: the next one's index.
        uint32_t m_currentColorCount {0};

        eastl::fixed_vector<OpenedScope, 8> m_passScopes;

        //! Shader accesses of a render pass declared without a stage: each must get one
        //! before EndPass.
        eastl::fixed_vector<RHIHandle, 8> m_unstagedAttachments;

        eastl::unordered_map<Pass, PassNode> m_graph;

        eastl::unordered_map<AttachmentId, eastl::vector<AttachmentEntry>> m_attachmentUses;

        eastl::unordered_map<RHI::AttachmentId, ResourceEntry> m_resources;

        uint32_t m_frameIndex { 0 };

        // Frame-scoped inputs seeded by Begin, not state.
        Math::Vector2Int m_renderSize { 0, 0 };
        Math::Vector2Int m_outputSize { 0, 0 };

        RHI::RHIHandle m_curSwapChainResource;

        // Owned by RenderGraph; stand-ins for a missing previous frame come from here.
        RHI::ImagePool* m_imagePool { nullptr };
    };

    template<typename PassTag, typename ComponentT>
    void RenderGraphBuilder::ValidateUniqueSlot(const RHI::InputName& slot)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        auto passAttachments = rhiContext.GetView<PassTag, ComponentT>();
        passAttachments.each([&](RHIHandle, ComponentT& a)
        {
            ASSERT(slot != a.m_slotName,
                "Duplicate slot name {} in same pass.", slot.GetCStr());
        });
    }

    // ============================================================
    // Version helpers
    // ============================================================

    inline uint32_t RenderGraphBuilder::LookupLatestVersion(const RHI::AttachmentId& name) const
    {
        auto it = m_resources.find(name);
        ASSERT(it != m_resources.end(),
            "AttachmentId '{}' has not been declared (Create / Import) yet. "
            "Passes must be declared in dependency order, a Read/Write must "
            "appear after the corresponding Create/Import.",
            name.GetCStr());
        return it->second.m_latestVersion;
    }

    inline uint32_t RenderGraphBuilder::BumpVersion(const RHI::AttachmentId& name)
    {
        auto it = m_resources.find(name);
        ASSERT(it != m_resources.end(),
            "AttachmentId '{}' has not been declared (Create / Import) yet. "
            "Passes must be declared in dependency order — a Write must "
            "appear after the corresponding Create/Import.",
            name.GetCStr());
        return ++(it->second.m_latestVersion);
    }

    // ============================================================
    // Registration helpers
    // ============================================================

    inline RHIHandle RenderGraphBuilder::FindImportedResource(const RHI::AttachmentId& name) const
    {
        auto it = m_resources.find(name);
        if (it == m_resources.end() || !RHIExecuteContext::Current()->Has<ImportedTag>(it->second.m_resource))
        {
            return NullHandle;
        }
        return it->second.m_resource;
    }

    inline RHIHandle RenderGraphBuilder::FindTransientImage(const RHI::AttachmentId& name) const
    {
        auto it = m_resources.find(name);
        if (it == m_resources.end())
        {
            return NullHandle;
        }
        const auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle resource = it->second.m_resource;
        return rhiContext.Has<TransientTag>(resource) && rhiContext.Has<RHI::ImageDescriptor>(resource)
            ? resource : NullHandle;
    }

    inline RHIHandle RenderGraphBuilder::FindPreviousFrameImage(const RHI::AttachmentId& name)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        for (auto [pooled, of] : rhiContext.GetView<PooledImageTag, PreviousFrameOf>().each())
        {
            if (of.m_name == name)
            {
                return pooled;
            }
        }
        return NullHandle;
    }

    template<typename PassTag>
    RHIHandle RenderGraphBuilder::RegisterBufferAttachment(BufferPassAttachment attachment)
    {
        // An earlier frame's copy never resolves against this frame's resources.
        if (attachment.m_buffer == NullHandle && attachment.m_attachmentId.m_frameOffset == 0)
        {
            attachment.m_buffer = FindImportedResource(attachment.m_attachmentId.m_id);
        }
        const RHIHandle attachmentHandle = AddBufferAttachment(attachment, CurrentScope());
        RHIExecuteContext::Current()->Add<PassTag>(attachmentHandle);
        return attachmentHandle;
    }

    template<typename PassTag>
    RHIHandle RenderGraphBuilder::RegisterImageAttachment(ImagePassAttachment attachment)
    {
        if (attachment.m_image == NullHandle && attachment.m_attachmentId.m_frameOffset == 0)
        {
            attachment.m_image = FindImportedResource(attachment.m_attachmentId.m_id);
        }
        const RHIHandle attachmentHandle = AddImageAttachment(attachment, CurrentScope(), &m_currentColorCount);
        RHIExecuteContext::Current()->Add<PassTag>(attachmentHandle);
        return attachmentHandle;
    }

    inline RHIHandle RenderGraphBuilder::CreateTransientImageResource(
        const RHI::AttachmentId&    name,
        const RHI::ImageDescriptor& desc,
        const RHI::ClearValue*      clearValue)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        RHIHandle resource = rhiContext.CreateEntity();
        rhiContext.Add<TransientTag>(resource);
        rhiContext.Add<ResourceName>(resource, ResourceName{ name });
        if (clearValue != nullptr)
        {
            rhiContext.Add<RHI::ClearValue>(resource, *clearValue);
        }
        rhiContext.Add<RHI::ImageDescriptor>(resource, desc);
        return resource;
    }

    inline RHIHandle RenderGraphBuilder::CreateTransientBufferResource(
        const RHI::AttachmentId&     name,
        const RHI::BufferDescriptor& desc)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        RHIHandle resource = rhiContext.CreateEntity();
        rhiContext.Add<TransientTag>(resource);
        rhiContext.Add<ResourceName>(resource, ResourceName{ name });
        rhiContext.Add<RHI::BufferDescriptor>(resource, desc);
        return resource;
    }

    // ============================================================
    // ImportImageAttachment / ImportBufferAttachment
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::ImportImageAttachment(
        const RHI::AttachmentId&                name,
        const ImportedImageAttachmentBindInfo&  bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        ImportResource(name, bind.m_image);

        ImagePassAttachment a;
        a.m_attachmentId      = AttachmentId{ name, 0 };
        a.m_slotName          = bind.m_slot;
        a.m_resolveSourceSlot = bind.m_resolveSourceSlot;
        a.m_access            = bind.m_access;
        a.m_usage             = bind.m_usage;
        a.m_stage             = bind.m_stage;
        a.m_action            = bind.m_action;
        a.m_viewDescriptor    = bind.m_viewDescriptor;
        a.m_image             = bind.m_image;
        a.m_pass              = m_currentPass;
        RegisterImageAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportBufferAttachment(
        const RHI::AttachmentId&                 name,
        const ImportedBufferAttachmentBindInfo&  bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        ImportResource(name, bind.m_buffer);

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, 0 };
        a.m_slotName        = bind.m_slot;
        a.m_access          = bind.m_access;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_viewDescriptor;
        a.m_buffer          = bind.m_buffer;
        a.m_pass            = m_currentPass;
        RegisterBufferAttachment<PassTag>(a);
    }

    // ============================================================
    // CreateImageAttachment / CreateBufferAttachment
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::CreateImageAttachment(
        const RHI::AttachmentId&        name,
        const RHI::ImageDescriptor&     desc,
        const ImageAttachmentBindInfo&  bind,
        RHI::AttachmentAccess           access)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ASSERT(m_resources.find(name) == m_resources.end(),
                "AttachmentId {} has already been declared (Create / Import).",
                name.GetCStr());
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        RHIHandle resource = CreateTransientImageResource(
            name, desc,
            bind.m_action.m_loadAction == RHI::AttachmentLoadAction::Clear ? &bind.m_action.m_clearValue : nullptr);

        ImagePassAttachment a;
        a.m_attachmentId      = AttachmentId{ name, 0 };
        a.m_slotName          = bind.m_slot;
        a.m_resolveSourceSlot = bind.m_resolveSourceSlot;
        a.m_access            = access;
        a.m_image             = resource;
        a.m_usage             = bind.m_usage;
        a.m_stage             = bind.m_stage;
        a.m_action            = bind.m_action;
        a.m_viewDescriptor    = bind.m_view;
        a.m_pass              = m_currentPass;

        m_resources.emplace(name, ResourceEntry{ resource, 0 });
        RegisterImageAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateBufferAttachment(
        const RHI::AttachmentId&        name,
        const RHI::BufferDescriptor&    desc,
        const BufferAttachmentBindInfo& bind,
        RHI::AttachmentAccess           access)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ASSERT(m_resources.find(name) == m_resources.end(),
                "AttachmentId {} has already been declared (Create / Import).",
                name.GetCStr());
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        RHIHandle resource = CreateTransientBufferResource(name, desc);

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, 0 };
        a.m_slotName        = bind.m_slot;
        a.m_access          = access;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_view;
        a.m_buffer          = resource;
        a.m_pass            = m_currentPass;

        m_resources.emplace(name, ResourceEntry{ resource, 0 });
        RegisterBufferAttachment<PassTag>(a);
    }

    // ============================================================
    // ReadImageAttachment / WriteImageAttachment / ReadWriteImageAttachment
    // ============================================================

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadImageAttachment(
        const RHI::AttachmentId&        name,
        const ImageAttachmentBindInfo&  bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        ImagePassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, LookupLatestVersion(name) };
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::Read;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_action          = bind.m_action;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        RegisterImageAttachment<PassTag>(a);
        return a.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::WriteImageAttachment(
        const RHI::AttachmentId&        name,
        const ImageAttachmentBindInfo&  bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        // Consume the current latest version (graph-only entry; BuildGraph turns
        // this into a "previous writer → this pass" edge). No ECS attachment is
        // created for it, so slot uniqueness is unaffected.
        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        ImagePassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, newVersion };
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::Write;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_action          = bind.m_action;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        RegisterImageAttachment<PassTag>(a);
        return a.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadWriteImageAttachment(
        const RHI::AttachmentId&        name,
        const ImageAttachmentBindInfo&  bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        ImagePassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, newVersion };
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::ReadWrite;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_action          = bind.m_action;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        RegisterImageAttachment<PassTag>(a);
        return a.m_attachmentId;
    }

    // ============================================================
    // ReadPreviousImageAttachment
    // ============================================================

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadPreviousImageAttachment(
        const RHI::AttachmentId&        name,
        const ImageAttachmentBindInfo&  bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        ImagePassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, 0, 1 };
        a.m_slotName        = bind.m_slot;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_action          = bind.m_action;
        a.m_viewDescriptor  = bind.m_view;

        const RHIHandle handle = AddPreviousFrameAttachment(a, CurrentScope());
        if (handle != NullHandle)
        {
            RHIExecuteContext::Current()->Add<PassTag>(handle);
        }
        return a.m_attachmentId;
    }

    // ============================================================
    // ReadBufferAttachment / WriteBufferAttachment / ReadWriteBufferAttachment
    // ============================================================

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadBufferAttachment(
        const RHI::AttachmentId&        name,
        const BufferAttachmentBindInfo& bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, LookupLatestVersion(name) };
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::Read;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        RegisterBufferAttachment<PassTag>(a);
        return a.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::WriteBufferAttachment(
        const RHI::AttachmentId&        name,
        const BufferAttachmentBindInfo& bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, newVersion };
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::Write;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        RegisterBufferAttachment<PassTag>(a);
        return a.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadWriteBufferAttachment(
        const RHI::AttachmentId&        name,
        const BufferAttachmentBindInfo& bind)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, newVersion };
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::ReadWrite;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        RegisterBufferAttachment<PassTag>(a);
        return a.m_attachmentId;
    }

}
