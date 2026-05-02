#pragma once

#include <EASTL/unordered_set.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <Log/SpdLogSystem.h>

#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>

namespace Spark::Render
{
    class RenderGraphBuilder
    {
    public:
        RenderGraphBuilder() = default;
        ~RenderGraphBuilder() = default;

        template<typename PassTag>
        void ImportBufferAttachment(const BufferPassAttachment& attachment);

        template<typename PassTag>
        void ImportBufferAttachment(
            const AttachmentId& id,
            const RHI::InputName& slot,
            RHIHandle view,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any
        );

        template<typename PassTag>
        void ImportImageAttachment(const ImagePassAttachment& attachment);

        template<typename PassTag>
        void ImportImageAttachment(
            const AttachmentId& id,
            const RHI::InputName& slot,
            RHIHandle view,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction()
        );

        template<typename PassTag>
        void CreateBufferAttachment(const BufferPassAttachment& attachment);

        template<typename PassTag>
        void CreateBufferAttachment(
            const AttachmentId& id,
            const RHI::InputName& slot,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any
        );

        template<typename PassTag>
        void CreateImageAttachment(const ImagePassAttachment& attachment);

        template<typename PassTag>
        void CreateImageAttachment(
            const AttachmentId& id,
            const RHI::InputName& slot,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction()
        );

        // Read* / Write* — auto-version helpers. The bare RHI::AttachmentId names
        // a previously created/imported attachment; the builder resolves the
        // version. Read uses the current latest. Write first injects an implicit
        // Read on the current latest (modeling "consume current content"), then
        // bumps the version and registers the Write — so BuildGraph chains the
        // producers in order.
        template<typename PassTag>
        AttachmentId ReadBufferAttachment(
            const RHI::AttachmentId& name,
            const RHI::InputName& slot,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any
        );

        template<typename PassTag>
        AttachmentId WriteBufferAttachment(
            const RHI::AttachmentId& name,
            const RHI::InputName& slot,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any
        );

        template<typename PassTag>
        AttachmentId ReadImageAttachment(
            const RHI::AttachmentId& name,
            const RHI::InputName& slot,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction()
        );

        template<typename PassTag>
        AttachmentId WriteImageAttachment(
            const RHI::AttachmentId& name,
            const RHI::InputName& slot,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction()
        );

        // ReadWrite* — single-slot read-modify-write. Same version semantics as
        // Write* (consume current latest, then publish a new version) but the
        // ECS attachment is registered with access = ReadWrite, so subsequent
        // passes see this pass as both consumer of v_N and producer of v_(N+1).
        // For images, callers typically pair this with LoadAction = Load /
        // StoreAction = Store; for buffers (no LoadStoreAction) this is the
        // only way to express RMW on a single slot.
        template<typename PassTag>
        AttachmentId ReadWriteBufferAttachment(
            const RHI::AttachmentId& name,
            const RHI::InputName& slot,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any
        );

        template<typename PassTag>
        AttachmentId ReadWriteImageAttachment(
            const RHI::AttachmentId& name,
            const RHI::InputName& slot,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction()
        );

    private:
        friend class RenderGraph;

        void AddEdge(Pass from, Pass to);

        void TouchNode(Pass pass);

        void BuildGraph();

        eastl::vector<Pass> TopoSort();

        void Begin();

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

        // Pure registration: build entity, attach components, record use. No validation.
        template<typename PassTag>
        void RegisterBufferAttachment(const BufferPassAttachment& attachment);

        template<typename PassTag>
        void RegisterImageAttachment(const ImagePassAttachment& attachment);

        // Validations.
        static void ValidateImportBufferAttachment(RHIHandle view, const AttachmentId& id);
        static void ValidateImportImageAttachment(RHIHandle view, const AttachmentId& id);

        template<typename PassTag>
        static void ValidateCreateBufferAttachment(RHIHandle view, const RHI::InputName& slot);

        template<typename PassTag>
        static void ValidateCreateImageAttachment(RHIHandle view, const RHI::InputName& slot);

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
    };

    // ============================================================
    // Validation helpers
    // ============================================================

    inline void RenderGraphBuilder::ValidateImportBufferAttachment(
        RHIHandle view, const AttachmentId& id)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<ViewHierarchy>(view), "The buffer view has not owned buffer.");
        RHIHandle buffer = rhiContext.Get<ViewHierarchy>(view).m_resource;
        ASSERT(buffer != NullHandle, "The owned buffer is null.");
        ASSERT(rhiContext.Has<ImportedTag>(buffer),
            "ImportBufferAttachment can only be used for imported resource.");
        ASSERT(rhiContext.Has<ResourceName>(view), "The view has no ResourceName component.");
        const auto& bufferName = rhiContext.Get<ResourceName>(buffer).m_name;
        ASSERT(id.m_id == bufferName,
            "AttachmentId {} does not match imported view ResourceName {}.",
            id.m_id.GetCStr(),
            bufferName.GetCStr());
    }

    inline void RenderGraphBuilder::ValidateImportImageAttachment(
        RHIHandle view, const AttachmentId& id)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<ViewHierarchy>(view), "The image view has not owned image.");
        RHIHandle image = rhiContext.Get<ViewHierarchy>(view).m_resource;
        ASSERT(image != NullHandle, "The owned image is null.");
        ASSERT(rhiContext.Has<ImportedTag>(image),
            "ImportImageAttachment can only be used for imported resource.");
        ASSERT(rhiContext.Has<ResourceName>(view), "The view has no ResourceName component.");
        const auto& imageName = rhiContext.Get<ResourceName>(image).m_name;
        ASSERT(id.m_id == imageName,
            "AttachmentId {} does not match imported view ResourceName {}.",
            id.m_id.GetCStr(),
            imageName.GetCStr());
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

    template<typename PassTag>
    void RenderGraphBuilder::ValidateCreateBufferAttachment(
        RHIHandle view, const RHI::InputName& slot)
    {
        ASSERT(view == NullHandle,
            "Transient resource should not be create before render graph compiling.");
        ValidateUniqueSlot<PassTag, BufferPassAttachment>(slot);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ValidateCreateImageAttachment(
        RHIHandle view, const RHI::InputName& slot)
    {
        ASSERT(view == NullHandle,
            "Transient resource should not be create before render graph compiling.");
        ValidateUniqueSlot<PassTag, ImagePassAttachment>(slot);
    }

    // ============================================================
    // Version helpers
    // ============================================================

    inline uint32_t RenderGraphBuilder::LookupLatestVersion(const RHI::AttachmentId& name) const
    {
        auto it = m_latestVersions.find(name);
        ASSERT(it != m_latestVersions.end(),
            "AttachmentId {} has not been created or imported.", name.GetCStr());
        return it->second;
    }

    inline uint32_t RenderGraphBuilder::BumpVersion(const RHI::AttachmentId& name)
    {
        auto it = m_latestVersions.find(name);
        ASSERT(it != m_latestVersions.end(),
            "AttachmentId {} has not been created or imported.", name.GetCStr());
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

    // ============================================================
    // ImportBufferAttachment
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::ImportBufferAttachment(const BufferPassAttachment& attachment)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateImportBufferAttachment(attachment.m_view, attachment.m_attachmentId);
        }
        BufferPassAttachment a = attachment;
        a.m_pass = m_currentPass;
        m_latestVersions.try_emplace(a.m_attachmentId.m_id, a.m_attachmentId.m_version);
        RegisterBufferAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportBufferAttachment(
        const AttachmentId& id,
        const RHI::InputName& slot,
        RHIHandle view,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage)
    {
        BufferPassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName     = slot;
        attachment.m_access       = access;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_view         = view;
        // m_pass is filled by the object-form below from m_currentPass.
        ImportBufferAttachment<PassTag>(attachment);
    }

    // ============================================================
    // ImportImageAttachment
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::ImportImageAttachment(const ImagePassAttachment& attachment)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateImportImageAttachment(attachment.m_view, attachment.m_attachmentId);
        }
        ImagePassAttachment a = attachment;
        a.m_pass = m_currentPass;
        m_latestVersions.try_emplace(a.m_attachmentId.m_id, a.m_attachmentId.m_version);
        RegisterImageAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportImageAttachment(
        const AttachmentId& id,
        const RHI::InputName& slot,
        RHIHandle view,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action)
    {
        ImagePassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName     = slot;
        attachment.m_access       = access;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_action       = action;
        attachment.m_view         = view;
        ImportImageAttachment<PassTag>(attachment);
    }

    // ============================================================
    // CreateBufferAttachment
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::CreateBufferAttachment(const BufferPassAttachment& attachment)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateCreateBufferAttachment<PassTag>(attachment.m_view, attachment.m_slotName);
        }
        BufferPassAttachment a = attachment;
        a.m_pass = m_currentPass;
        m_latestVersions.try_emplace(a.m_attachmentId.m_id, a.m_attachmentId.m_version);
        RegisterBufferAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateBufferAttachment(
        const AttachmentId& id,
        const RHI::InputName& slot,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage)
    {
        BufferPassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName     = slot;
        attachment.m_access       = access;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        CreateBufferAttachment<PassTag>(attachment);
    }

    // ============================================================
    // CreateImageAttachment
    // ============================================================

    template<typename PassTag>
    void RenderGraphBuilder::CreateImageAttachment(const ImagePassAttachment& attachment)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateCreateImageAttachment<PassTag>(attachment.m_view, attachment.m_slotName);
        }
        ImagePassAttachment a = attachment;
        a.m_pass = m_currentPass;
        m_latestVersions.try_emplace(a.m_attachmentId.m_id, a.m_attachmentId.m_version);
        RegisterImageAttachment<PassTag>(a);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateImageAttachment(
        const AttachmentId& id,
        const RHI::InputName& slot,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action)
    {
        ImagePassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName     = slot;
        attachment.m_access       = access;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_action       = action;
        CreateImageAttachment<PassTag>(attachment);
    }

    // ============================================================
    // ReadBufferAttachment / WriteBufferAttachment
    // ============================================================

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadBufferAttachment(
        const RHI::AttachmentId& name,
        const RHI::InputName& slot,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(slot);
        }

        BufferPassAttachment attachment;
        attachment.m_attachmentId = AttachmentId{ name, LookupLatestVersion(name) };
        attachment.m_slotName     = slot;
        attachment.m_access       = RHI::AttachmentAccess::Read;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_pass         = m_currentPass;
        RegisterBufferAttachment<PassTag>(attachment);
        return attachment.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::WriteBufferAttachment(
        const RHI::AttachmentId& name,
        const RHI::InputName& slot,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(slot);
        }

        // Consume the current latest version (graph-only entry; BuildGraph turns
        // this into a "previous writer → this pass" edge). No ECS attachment is
        // created for it, so slot uniqueness is unaffected.
        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        // Then publish a new version as the produced output.
        const uint32_t newVersion = BumpVersion(name);

        BufferPassAttachment attachment;
        attachment.m_attachmentId = AttachmentId{ name, newVersion };
        attachment.m_slotName     = slot;
        attachment.m_access       = RHI::AttachmentAccess::Write;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_pass         = m_currentPass;
        RegisterBufferAttachment<PassTag>(attachment);
        return attachment.m_attachmentId;
    }

    // ============================================================
    // ReadImageAttachment / WriteImageAttachment
    // ============================================================

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadImageAttachment(
        const RHI::AttachmentId& name,
        const RHI::InputName& slot,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(slot);
        }

        ImagePassAttachment attachment;
        attachment.m_attachmentId = AttachmentId{ name, LookupLatestVersion(name) };
        attachment.m_slotName     = slot;
        attachment.m_access       = RHI::AttachmentAccess::Read;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_action       = action;
        attachment.m_pass         = m_currentPass;
        RegisterImageAttachment<PassTag>(attachment);
        return attachment.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::WriteImageAttachment(
        const RHI::AttachmentId& name,
        const RHI::InputName& slot,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(slot);
        }

        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        ImagePassAttachment attachment;
        attachment.m_attachmentId = AttachmentId{ name, newVersion };
        attachment.m_slotName     = slot;
        attachment.m_access       = RHI::AttachmentAccess::Write;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_action       = action;
        attachment.m_pass         = m_currentPass;
        RegisterImageAttachment<PassTag>(attachment);
        return attachment.m_attachmentId;
    }

    // ============================================================
    // ReadWriteBufferAttachment / ReadWriteImageAttachment
    // ============================================================

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadWriteBufferAttachment(
        const RHI::AttachmentId& name,
        const RHI::InputName& slot,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, BufferPassAttachment>(slot);
        }

        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        BufferPassAttachment attachment;
        attachment.m_attachmentId = AttachmentId{ name, newVersion };
        attachment.m_slotName     = slot;
        attachment.m_access       = RHI::AttachmentAccess::ReadWrite;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_pass         = m_currentPass;
        RegisterBufferAttachment<PassTag>(attachment);
        return attachment.m_attachmentId;
    }

    template<typename PassTag>
    AttachmentId RenderGraphBuilder::ReadWriteImageAttachment(
        const RHI::AttachmentId& name,
        const RHI::InputName& slot,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action)
    {
        if constexpr (s_buildValidation)
        {
            ASSERT(m_currentPass != NullPass,
                "BeginPass must be called before declaring attachments.");
            ValidateUniqueSlot<PassTag, ImagePassAttachment>(slot);
        }

        const uint32_t latestVersion = LookupLatestVersion(name);
        m_attachmentUses[AttachmentId{ name, latestVersion }]
            .emplace_back(m_currentPass, RHI::AttachmentAccess::Read);

        const uint32_t newVersion = BumpVersion(name);

        ImagePassAttachment attachment;
        attachment.m_attachmentId = AttachmentId{ name, newVersion };
        attachment.m_slotName     = slot;
        attachment.m_access       = RHI::AttachmentAccess::ReadWrite;
        attachment.m_usage        = usage;
        attachment.m_stage        = stage;
        attachment.m_action       = action;
        attachment.m_pass         = m_currentPass;
        RegisterImageAttachment<PassTag>(attachment);
        return attachment.m_attachmentId;
    }

}
