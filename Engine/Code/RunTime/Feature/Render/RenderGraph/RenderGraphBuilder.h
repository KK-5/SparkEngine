#pragma once

#include <EASTL/unordered_set.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <Log/ILogSystem.h>

#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <RHI/Context/RHIContext.h>

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

    //! Per-call binding info for an imported image attachment. The view is a
    //! fully-formed RHIHandle (an entity with ImageView / ViewHierarchy already
    //! present) supplied by the caller — the render graph never builds it.
    //! Access is part of this struct because the same imported view may be
    //! consumed with different access in different passes.
    struct ImportedImageAttachmentBindInfo
    {
        RHI::InputName                 m_slot;
        RHI::InputName                 m_resolveSourceSlot;
        RHIHandle                      m_view  {NullHandle};
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
        RHIHandle                 m_view  {NullHandle};
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

    private:
        friend class RenderGraph;

        void AddEdge(Pass from, Pass to);

        void TouchNode(Pass pass);

        void BuildGraph();

        eastl::vector<Pass> TopoSort();

        void Begin(uint32_t frameIndex);

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

        // Pure registration: build attachment entity, attach components, record use. No validation.
        template<typename PassTag>
        void RegisterBufferAttachment(const BufferPassAttachment& attachment);

        template<typename PassTag>
        void RegisterImageAttachment(const ImagePassAttachment& attachment);

        // Materialize a transient resource entity in RHIContext: TransientTag,
        // ResourceName, plus the resource descriptor.
        RHIHandle CreateTransientImageResource(
            const RHI::AttachmentId&    name,
            const RHI::ImageDescriptor& desc);

        RHIHandle CreateTransientBufferResource(
            const RHI::AttachmentId&     name,
            const RHI::BufferDescriptor& desc);

        // Validations.
        static void ValidateImportedView(RHIHandle view, const RHI::AttachmentId& name, bool image);

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

        Pass m_currentPass {NullPass};

        eastl::unordered_map<Pass, PassNode> m_graph;

        eastl::unordered_map<AttachmentId, eastl::vector<AttachmentEntry>> m_attachmentUses;

        // bare-name → latest produced version. Seeded by Create/Import; bumped by Write*.
        eastl::unordered_map<RHI::AttachmentId, uint32_t> m_latestVersions;

        uint32_t m_frameIndex { 0 };
    };

    // ============================================================
    // Validation helpers
    // ============================================================

    inline void RenderGraphBuilder::ValidateImportedView(
        RHIHandle view, const RHI::AttachmentId& name, bool image)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(view != NullHandle, "Imported attachment requires a non-null view handle.");
        ASSERT(rhiContext.Has<ViewHierarchy>(view), "The view has no ViewHierarchy.");
        RHIHandle resource = rhiContext.Get<ViewHierarchy>(view).m_resource;
        ASSERT(resource != NullHandle, "The owned resource is null.");
        // ImportedTag is no longer a precondition — Import*Attachment auto-attaches
        // it. Caller only needs the owning resource/view components in place.
        ASSERT(rhiContext.Has<ResourceName>(view), "The view has no ResourceName component.");
        const auto& resourceName = rhiContext.Get<ResourceName>(resource).m_name;
        ASSERT(name == resourceName,
            "AttachmentId {} does not match imported resource ResourceName {}.",
            name.GetCStr(),
            resourceName.GetCStr());
        (void)image;
    }

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
        auto it = m_latestVersions.find(name);
        ASSERT(it != m_latestVersions.end(),
            "AttachmentId '{}' has not been declared (Create / Import) yet. "
            "Passes must be declared in dependency order — a Read/Write must "
            "appear after the corresponding Create/Import.",
            name.GetCStr());
        return it->second;
    }

    inline uint32_t RenderGraphBuilder::BumpVersion(const RHI::AttachmentId& name)
    {
        auto it = m_latestVersions.find(name);
        ASSERT(it != m_latestVersions.end(),
            "AttachmentId '{}' has not been declared (Create / Import) yet. "
            "Passes must be declared in dependency order — a Write must "
            "appear after the corresponding Create/Import.",
            name.GetCStr());
        return ++(it->second);
    }

    // ============================================================
    // Registration helpers
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::RegisterBufferAttachment(const BufferPassAttachment& attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[attachment.m_attachmentId].emplace_back(
            attachment.m_pass, attachment.m_access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::RegisterImageAttachment(const ImagePassAttachment& attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[attachment.m_attachmentId].emplace_back(
            attachment.m_pass,
            NormalizeImageAccess(attachment.m_access, attachment.m_action));
    }

    inline RHIHandle RenderGraphBuilder::CreateTransientImageResource(
        const RHI::AttachmentId&    name,
        const RHI::ImageDescriptor& desc)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        RHIHandle resource = rhiContext.CreateEntity();
        rhiContext.Add<TransientTag>(resource);
        rhiContext.Add<ResourceName>(resource, ResourceName{ name });
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
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateImportedView(bind.m_view, name, /*image=*/true);
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        RHIHandle resource = rhiContext.Get<ViewHierarchy>(bind.m_view).m_resource;

        // Auto-attach ImportedTag on the resource entity.
        // ImportedTag is a resource-level concept — views inherit their
        // lifecycle classification from the parent resource via ViewHierarchy.
        if (!rhiContext.Has<ImportedTag>(resource))
            rhiContext.Add<ImportedTag>(resource);

        // Materialize BackingImage / BackingImageView from the owning resource.
        // Single-frame variants (Image / ImageView) write once and never change.
        // Per-frame variants (ImagePerFrame / ImageViewPerFrame) point at the
        // current frame's slot — refreshed every frame by
        // RenderGraph::RefreshPerFrameBackings for already-imported entities;
        // first-frame imports are handled here using m_frameIndex.
        if (auto* img = rhiContext.TryGet<Image>(resource))
        {
            if (!rhiContext.Has<BackingImage>(resource))
                rhiContext.Add<BackingImage>(resource, BackingImage{ img->m_image.get() });
        }
        else if (auto* imgPF = rhiContext.TryGet<ImagePerFrame>(resource))
        {
            rhiContext.AddOrReplace<BackingImage>(resource,
                BackingImage{ imgPF->m_images[m_frameIndex].get() });
        }

        if (auto* view = rhiContext.TryGet<ImageView>(bind.m_view))
        {
            if (!rhiContext.Has<BackingImageView>(bind.m_view))
                rhiContext.Add<BackingImageView>(bind.m_view, BackingImageView{ view->m_view.get() });
        }
        else if (auto* viewPF = rhiContext.TryGet<ImageViewPerFrame>(bind.m_view))
        {
            rhiContext.AddOrReplace<BackingImageView>(bind.m_view,
                BackingImageView{ viewPF->m_views[m_frameIndex].get() });
        }

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<BackingImage>(resource),
                "Imported resource {} has no BackingImage. Caller must attach "
                "Image (single-frame) or ImagePerFrame (per-frame) before importing.",
                name.GetCStr());
            ASSERT(rhiContext.Has<BackingImageView>(bind.m_view),
                "Imported view for {} has no BackingImageView. Caller must attach "
                "ImageView (single-frame) or ImageViewPerFrame (per-frame) before importing.",
                name.GetCStr());
        }

        ImagePassAttachment a;
        a.m_attachmentId      = AttachmentId{ name, 0 };
        a.m_slotName          = bind.m_slot;
        a.m_resolveSourceSlot = bind.m_resolveSourceSlot;
        a.m_access            = bind.m_access;
        a.m_usage             = bind.m_usage;
        a.m_stage             = bind.m_stage;
        a.m_action            = bind.m_action;
        a.m_view              = bind.m_view;
        a.m_pass              = m_currentPass;

        m_latestVersions.emplace(name, 0u);
        RegisterImageAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportBufferAttachment(
        const RHI::AttachmentId&                 name,
        const ImportedBufferAttachmentBindInfo&  bind)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateImportedView(bind.m_view, name, /*image=*/false);
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        RHIHandle resource = rhiContext.Get<ViewHierarchy>(bind.m_view).m_resource;

        // Auto-attach ImportedTag on the resource entity (same rationale as
        // ImportImageAttachment — resource-level concept, not view-level).
        if (!rhiContext.Has<ImportedTag>(resource))
            rhiContext.Add<ImportedTag>(resource);

        // See ImportImageAttachment for the Backing materialization rationale.
        if (auto* buf = rhiContext.TryGet<Buffer>(resource))
        {
            if (!rhiContext.Has<BackingBuffer>(resource))
                rhiContext.Add<BackingBuffer>(resource, BackingBuffer{ buf->m_buffer.get() });
        }
        else if (auto* bufPF = rhiContext.TryGet<BufferPerFrame>(resource))
        {
            rhiContext.AddOrReplace<BackingBuffer>(resource,
                BackingBuffer{ bufPF->m_buffers[m_frameIndex].get() });
        }

        if (auto* view = rhiContext.TryGet<BufferView>(bind.m_view))
        {
            if (!rhiContext.Has<BackingBufferView>(bind.m_view))
                rhiContext.Add<BackingBufferView>(bind.m_view, BackingBufferView{ view->m_view.get() });
        }
        else if (auto* viewPF = rhiContext.TryGet<BufferViewPerFrame>(bind.m_view))
        {
            rhiContext.AddOrReplace<BackingBufferView>(bind.m_view,
                BackingBufferView{ viewPF->m_views[m_frameIndex].get() });
        }

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<BackingBuffer>(resource),
                "Imported resource {} has no BackingBuffer. Caller must attach "
                "Buffer (single-frame) or BufferPerFrame (per-frame) before importing.",
                name.GetCStr());
            ASSERT(rhiContext.Has<BackingBufferView>(bind.m_view),
                "Imported view for {} has no BackingBufferView. Caller must attach "
                "BufferView (single-frame) or BufferViewPerFrame (per-frame) before importing.",
                name.GetCStr());
        }

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, 0 };
        a.m_slotName        = bind.m_slot;
        a.m_access          = bind.m_access;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_view            = bind.m_view;
        a.m_pass            = m_currentPass;

        m_latestVersions.emplace(name, 0u);
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
            ASSERT(m_latestVersions.find(name) == m_latestVersions.end(),
                "AttachmentId {} has already been declared (Create / Import).",
                name.GetCStr());
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        CreateTransientImageResource(name, desc);

        ImagePassAttachment a;
        a.m_attachmentId      = AttachmentId{ name, 0 };
        a.m_slotName          = bind.m_slot;
        a.m_resolveSourceSlot = bind.m_resolveSourceSlot;
        a.m_access            = access;
        a.m_usage             = bind.m_usage;
        a.m_stage             = bind.m_stage;
        a.m_action            = bind.m_action;
        a.m_viewDescriptor    = bind.m_view;
        a.m_pass              = m_currentPass;

        m_latestVersions.emplace(name, 0u);
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
            ASSERT(m_latestVersions.find(name) == m_latestVersions.end(),
                "AttachmentId {} has already been declared (Create / Import).",
                name.GetCStr());
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        CreateTransientBufferResource(name, desc);

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, 0 };
        a.m_slotName        = bind.m_slot;
        a.m_access          = access;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;

        m_latestVersions.emplace(name, 0u);
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
