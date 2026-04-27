#pragma once

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
            const RHI::AttachmentId& id, 
            const RHI::InputName& slot,
            RHIHandle view,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            Pass pass = NullPass
        );

        template<typename PassTag>
        void ImportImageAttachment(const ImagePassAttachment& attachment);

        template<typename PassTag>
        void ImportImageAttachment(
            const RHI::AttachmentId& id, 
            const RHI::InputName& slot,
            RHIHandle view,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction(),
            Pass pass = NullPass
        );

        template<typename PassTag>
        void CreateBufferAttachment(const BufferPassAttachment& attachment);

        template<typename PassTag>
        void CreateBufferAttachment(
            const RHI::AttachmentId& id, 
            const RHI::InputName& slot,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            Pass pass = NullPass
        );

        template<typename PassTag>
        void CreateImageAttachment(const ImagePassAttachment& attachment);

        template<typename PassTag>
        void CreateImageAttachment(
            const RHI::AttachmentId& id, 
            const RHI::InputName& slot,
            RHI::AttachmentAccess access = RHI::AttachmentAccess::Unknown,
            RHI::AttachmentUsage usage = RHI::AttachmentUsage::Uninitialized,
            RHI::AttachmentStage stage = RHI::AttachmentStage::Any,
            const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction(),
            Pass pass = NullPass
        );

    private:
        friend class RenderGraph;

        void BuildGraph();

        eastl::vector<Pass> End();

        static constexpr bool s_buildValidation { true };

        struct AttachmentEntry
        {
            Pass pass;
            RHI::AttachmentAccess access;
        };

        eastl::unordered_map<RHI::AttachmentId, eastl::vector<AttachmentEntry>> m_attachmentUses;
    };


    template<typename PassTag>
    void RenderGraphBuilder::ImportBufferAttachment(const BufferPassAttachment& attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<ViewHierarchy>(attachment.m_view), "The buffer view has not owned buffer.");
            RHIHandle buffer = rhiContext.Get<ViewHierarchy>(attachment.m_view).m_resource;
            ASSERT(buffer != NullHandle, "The owned buffer is null.");
            ASSERT(rhiContext.Has<ImportedTag>(buffer), "ImportBufferAttachment can only be used for imported resource.");
            ASSERT(rhiContext.Has<ResourceName>(attachment.m_view), "The view has no ResourceName component.");
            const auto& viewName = rhiContext.Get<ResourceName>(attachment.m_view).m_name;
            ASSERT(attachment.m_attachmentId == viewName,
                "AttachmentId {} does not match imported view ResourceName {}.",
                attachment.m_attachmentId.GetCStr(),
                viewName.GetCStr());
        }

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[attachment.m_attachmentId].emplace_back(attachment.m_pass, attachment.m_access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportBufferAttachment(
        const RHI::AttachmentId& id, 
        const RHI::InputName& slot,
        RHIHandle view,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        Pass pass
    )
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<ViewHierarchy>(view), "The buffer view has not owned buffer.");
            RHIHandle buffer = rhiContext.Get<ViewHierarchy>(view).m_resource;
            ASSERT(buffer != NullHandle, "The owned buffer is null.");
            ASSERT(rhiContext.Has<ImportedTag>(buffer), "ImportBufferAttachment can only be used for imported resource.");
            ASSERT(rhiContext.Has<ResourceName>(view), "The view has no ResourceName component.");
            const auto& viewName = rhiContext.Get<ResourceName>(view).m_name;
            ASSERT(id == viewName,
                "AttachmentId {} does not match imported view ResourceName {}.",
                id.GetCStr(),
                viewName.GetCStr());
        }

        BufferPassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName = slot;
        attachment.m_access = access;
        attachment.m_usage = usage;
        attachment.m_stage = stage;
        attachment.m_view = view;
        attachment.m_pass = pass;

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[id].emplace_back(pass, access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportImageAttachment(const ImagePassAttachment& attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<ViewHierarchy>(attachment.m_view), "The image view has not owned image.");
            RHIHandle image = rhiContext.Get<ViewHierarchy>(attachment.m_view).m_resource;
            ASSERT(image != NullHandle, "The owned image is null.");
            ASSERT(rhiContext.Has<ImportedTag>(image), "ImportImageAttachment can only be used for imported resource.");
            ASSERT(rhiContext.Has<ResourceName>(attachment.m_view), "The view has no ResourceName component.");
            const auto& viewName = rhiContext.Get<ResourceName>(attachment.m_view).m_name;
            ASSERT(attachment.m_attachmentId == viewName,
                "AttachmentId {} does not match imported view ResourceName {}.",
                attachment.m_attachmentId.GetCStr(),
                viewName.GetCStr());
        }

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[attachment.m_attachmentId].emplace_back(attachment.m_pass, attachment.m_access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::ImportImageAttachment(
        const RHI::AttachmentId& id, 
        const RHI::InputName& slot,
        RHIHandle view,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action,
        Pass pass
    )
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(rhiContext.Has<ViewHierarchy>(view), "The image view has not owned image.");
            RHIHandle image = rhiContext.Get<ViewHierarchy>(view).m_resource;
            ASSERT(image != NullHandle, "The owned image is null.");
            ASSERT(rhiContext.Has<ImportedTag>(image), "ImportImageAttachment can only be used for imported resource.");
            ASSERT(rhiContext.Has<ResourceName>(view), "The view has no ResourceName component.");
            const auto& viewName = rhiContext.Get<ResourceName>(view).m_name;
            ASSERT(id == viewName,
                "AttachmentId {} does not match imported view ResourceName {}.",
                id.GetCStr(),
                viewName.GetCStr());
        }

        ImagePassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName = slot;
        attachment.m_access = access;
        attachment.m_usage = usage;
        attachment.m_stage = stage;
        attachment.m_action = action;
        attachment.m_view = view;
        attachment.m_pass = pass;

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[id].emplace_back(pass, access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateBufferAttachment(const BufferPassAttachment& attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(attachment.m_view == NullHandle, "Transient resource should not be create before render graph compiling.");
            auto passAttachments = rhiContext.GetView<PassTag, BufferPassAttachment>();
            passAttachments.each([&](RHIHandle handle, BufferPassAttachment& a)
            {
                ASSERT(attachment.m_slotName != a.m_slotName, "Duplicate slot name {} in same pass.", attachment.m_slotName.GetCStr());
            });
        }

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[attachment.m_attachmentId].emplace_back(attachment.m_pass, attachment.m_access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateBufferAttachment(
        const RHI::AttachmentId& id, 
        const RHI::InputName& slot,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        Pass pass
    )
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            auto passAttachments = rhiContext.GetView<PassTag, BufferPassAttachment>();
            passAttachments.each([&](RHIHandle handle, BufferPassAttachment& a)
            {
                ASSERT(slot != a.m_slotName, "Duplicate slot name {} in same pass.", slot.GetCStr());
            });
        }

        BufferPassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName = slot;
        attachment.m_access = access;
        attachment.m_usage = usage;
        attachment.m_stage = stage;
        attachment.m_pass = pass;

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<BufferPassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[id].emplace_back(pass, access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateImageAttachment(const ImagePassAttachment& attachment)
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            ASSERT(attachment.m_view == NullHandle, "Transient resource should not be create before render graph compiling.");
            auto passAttachments = rhiContext.GetView<PassTag, ImagePassAttachment>();
            passAttachments.each([&](RHIHandle handle, ImagePassAttachment& a)
            {
                ASSERT(attachment.m_slotName != a.m_slotName, "Duplicate slot name {} in same pass.", attachment.m_slotName.GetCStr());
            });
        }

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[attachment.m_attachmentId].emplace_back(attachment.m_pass, attachment.m_access);
    }

    template<typename PassTag>
    void RenderGraphBuilder::CreateImageAttachment(
        const RHI::AttachmentId& id, 
        const RHI::InputName& slot,
        RHI::AttachmentAccess access,
        RHI::AttachmentUsage usage,
        RHI::AttachmentStage stage,
        const RHI::AttachmentLoadStoreAction& action,
        Pass pass
    )
    {
        auto& rhiContext = *RHIExecuteContext::Current();

        if constexpr (s_buildValidation)
        {
            auto passAttachments = rhiContext.GetView<PassTag, ImagePassAttachment>();
            passAttachments.each([&](RHIHandle handle, ImagePassAttachment& a)
            {
                ASSERT(slot != a.m_slotName, "Duplicate slot name {} in same pass.", slot.GetCStr());
            });
        }

        ImagePassAttachment attachment;
        attachment.m_attachmentId = id;
        attachment.m_slotName = slot;
        attachment.m_access = access;
        attachment.m_usage = usage;
        attachment.m_stage = stage;
        attachment.m_action = action;
        attachment.m_pass = pass;

        RHIHandle attachmentHandle = rhiContext.CreateEntity();
        rhiContext.Add<ImagePassAttachment>(attachmentHandle, attachment);
        rhiContext.Add<PassTag>(attachmentHandle);
        m_attachmentUses[id].emplace_back(pass, access);
    }

}
