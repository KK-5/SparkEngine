#pragma once

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

        // Pure registration: build attachment entity, attach components, record use. No validation.
        // Takes the attachment by value: resolves an imported resource link (m_image/
        // m_buffer) at declaration time when it was not set by the caller.
        template<typename PassTag>
        RHIHandle RegisterBufferAttachment(BufferPassAttachment attachment);

        template<typename PassTag>
        RHIHandle RegisterImageAttachment(ImagePassAttachment attachment);

        // Resolve an attachment name to an imported resource entity (ImportedTag).
        // Imported resources exist from import time (before any Build), so the link is
        // set right here at declaration. Returns NullHandle when the name is not an
        // imported resource — then it is a transient name, resolved later by
        // CompileTransientResources.
        static RHIHandle FindImportedResourceByName(const RHI::AttachmentId& name);

        // Counterpart for transient resources, which exist from the Create that declared
        // them. Used to link a previous-frame read to the resource it mirrors.
        static RHIHandle FindTransientImageByName(const RHI::AttachmentId& name);

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

        Pass m_currentPass {NullPass};

        //! The one Scope every pass has until passes can declare more.
        RHIHandle m_currentScope {NullHandle};

        //! RenderTarget attachments declared in m_currentScope so far: the next one's index.
        uint32_t m_currentColorCount {0};

        eastl::unordered_map<Pass, PassNode> m_graph;

        eastl::unordered_map<AttachmentId, eastl::vector<AttachmentEntry>> m_attachmentUses;

        // bare-name → latest produced version. Seeded by Create/Import; bumped by Write*.
        eastl::unordered_map<RHI::AttachmentId, uint32_t> m_latestVersions;

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
        auto it = m_latestVersions.find(name);
        ASSERT(it != m_latestVersions.end(),
            "AttachmentId '{}' has not been declared (Create / Import) yet. "
            "Passes must be declared in dependency order, a Read/Write must "
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

    inline RHIHandle RenderGraphBuilder::FindImportedResourceByName(const RHI::AttachmentId& name)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        for (auto [resource, rn] : rhiContext.GetView<ImportedTag, ResourceName>().each())
        {
            if (rn.m_name == name)
            {
                return resource;
            }
        }
        return NullHandle;
    }

    inline RHIHandle RenderGraphBuilder::FindTransientImageByName(const RHI::AttachmentId& name)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        for (auto [resource, rn, desc] : rhiContext.GetView<TransientTag, ResourceName, RHI::ImageDescriptor>().each())
        {
            if (rn.m_name == name)
            {
                return resource;
            }
        }
        return NullHandle;
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
        auto& rhiContext = *RHIExecuteContext::Current();
        // An earlier frame's copy never resolves against this frame's resources.
        if (attachment.m_buffer == NullHandle && attachment.m_attachmentId.m_frameOffset == 0)
        {
            attachment.m_buffer = FindImportedResourceByName(attachment.m_attachmentId.m_id);
        }
        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        rhiContext.Add<ScopeAttachment>(attachmentHandle, ScopeAttachment{ m_currentScope });
        m_attachmentUses[attachment.m_attachmentId].emplace_back(
            attachment.m_pass, attachment.m_access);
        return attachmentHandle;
    }

    template<typename PassTag>
    RHIHandle RenderGraphBuilder::RegisterImageAttachment(ImagePassAttachment attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        if (attachment.m_image == NullHandle && attachment.m_attachmentId.m_frameOffset == 0)
        {
            attachment.m_image = FindImportedResourceByName(attachment.m_attachmentId.m_id);
        }
        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        rhiContext.Add<ScopeAttachment>(attachmentHandle, ScopeAttachment{ m_currentScope });
        if (attachment.m_usage == RHI::AttachmentUsage::RenderTarget)
        {
            rhiContext.Add<ColorAttachmentIndex>(attachmentHandle, ColorAttachmentIndex{ m_currentColorCount++ });
        }
        m_attachmentUses[attachment.m_attachmentId].emplace_back(
            attachment.m_pass,
            NormalizeImageAccess(attachment.m_access, attachment.m_action));
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
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(bind.m_slot);
        }

        RHIHandle resource = bind.m_image;
        ASSERT(resource != NullHandle, "ImportImageAttachment: bind.m_image is NullHandle.");

        // ImportedTag is a resource-level concept.
        if (!rhiContext.Has<ImportedTag>(resource))
        {
            rhiContext.Add<ImportedTag>(resource);
        }

        // Materialize BackingImage for single-frame imports from the owning Image
        // component (write once). Per-frame resources (ImagePerFrame / swap chain
        // SwapChainImages) get BackingImage refreshed every frame by
        // RenderGraph::RefreshPerFrameBackings, which runs before Build — nothing to do here.
        // The view itself is resolved on demand from the resource's view cache
        // (GetOrCreateImageView / GetOrCreateImageViewPerFrame), not materialized here.
        if (auto* img = rhiContext.TryGet<Image>(resource))
        {
            if (!rhiContext.Has<BackingImage>(resource))
            {
                rhiContext.Add<BackingImage>(resource, BackingImage{ img->m_image.get() });
            }
        }

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<BackingImage>(resource),
                "Imported resource {} has no BackingImage. Single-frame: attach an Image "
                "component before importing; per-frame: ensure RefreshPerFrameBackings ran "
                "(ImagePerFrame / SwapChainImages).",
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
        a.m_viewDescriptor    = bind.m_viewDescriptor;
        a.m_image             = resource;
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
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(bind.m_slot);
        }

        RHIHandle resource = bind.m_buffer;
        ASSERT(resource != NullHandle, "ImportBufferAttachment: bind.m_buffer is NullHandle.");

        if (!rhiContext.Has<ImportedTag>(resource))
        {
            rhiContext.Add<ImportedTag>(resource);
        }

        // Single-frame imports materialize BackingBuffer from the owning Buffer
        // component; per-frame resources get it refreshed by RefreshPerFrameBackings.
        // The view (if any) is resolved on demand from the resource's view cache.
        if (auto* buf = rhiContext.TryGet<Buffer>(resource))
        {
            if (!rhiContext.Has<BackingBuffer>(resource))
            {
                rhiContext.Add<BackingBuffer>(resource, BackingBuffer{ buf->m_buffer.get() });
            }
        }

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<BackingBuffer>(resource),
                "Imported resource {} has no BackingBuffer. Single-frame: attach a Buffer "
                "component before importing; per-frame: ensure RefreshPerFrameBackings ran.",
                name.GetCStr());
        }

        BufferPassAttachment a;
        a.m_attachmentId    = AttachmentId{ name, 0 };
        a.m_slotName        = bind.m_slot;
        a.m_access          = bind.m_access;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_viewDescriptor  = bind.m_viewDescriptor;
        a.m_buffer          = resource;
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

        const AttachmentId id{ name, 0, 1 };

        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle declared = FindTransientImageByName(name);
        ASSERT(declared != NullHandle,
            "Previous-frame read of '{}' before any pass created it as a transient image. "
            "Declare the producing pass first.",
            name.GetCStr());
        if (declared == NullHandle)
        {
            return id;
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

        // No m_latestVersions bump: nothing writes the previous frame's copy. The use
        // entry lands under a key of its own, so BuildGraph sees readers and no writer
        // and emits no edge — it still registers the pass as a node.
        ImagePassAttachment a;
        a.m_attachmentId    = id;
        a.m_slotName        = bind.m_slot;
        a.m_access          = RHI::AttachmentAccess::Read;
        a.m_usage           = bind.m_usage;
        a.m_stage           = bind.m_stage;
        a.m_action          = bind.m_action;
        a.m_viewDescriptor  = bind.m_view;
        a.m_pass            = m_currentPass;
        a.m_image           = previous;   // known here, like an import

        const RHIHandle handle = RegisterImageAttachment<PassTag>(a);
        rhiContext.Add<PreviousFrameTag>(handle);
        if (missing)
        {
            rhiContext.Add<PreviousFrameMissingTag>(handle);
        }
        return id;
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
