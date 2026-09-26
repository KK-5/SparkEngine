#include "PassScopes.h"

#include <RHI/Command/DrawItem.h>

namespace Spark::Render
{
    namespace
    {
        constexpr RHI::AttachmentStage DepthStages =
            RHI::AttachmentStage::EarlyFragmentTest | RHI::AttachmentStage::LateFragmentTest;
    }

    // ============================================================
    // Attachment / ShaderAttachment
    // ============================================================

    Attachment& Attachment::Format(RHI::Format format)
    {
        auto* image = RHIExecuteContext::Current()->TryGet<ImagePassAttachment>(m_handle);
        ASSERT(image != nullptr, "Format() is for image attachments.");
        image->m_viewDescriptor.m_overrideFormat = format;
        return *this;
    }

    Attachment& Attachment::View(const RHI::ImageViewDescriptor& view)
    {
        auto* image = RHIExecuteContext::Current()->TryGet<ImagePassAttachment>(m_handle);
        ASSERT(image != nullptr, "An image view on an attachment of a buffer.");
        image->m_viewDescriptor = view;
        return *this;
    }

    Attachment& Attachment::View(const RHI::BufferViewDescriptor& view)
    {
        auto* buffer = RHIExecuteContext::Current()->TryGet<BufferPassAttachment>(m_handle);
        ASSERT(buffer != nullptr, "A buffer view on an attachment of an image.");
        buffer->m_viewDescriptor = view;
        return *this;
    }

    ShaderAttachment& ShaderAttachment::Format(RHI::Format format)
    {
        auto* image = RHIExecuteContext::Current()->TryGet<ImagePassAttachment>(GetHandle());
        ASSERT(image != nullptr, "Format() is for image attachments.");
        const bool writes = (image->m_access & RHI::AttachmentAccess::Write) != RHI::AttachmentAccess::Unknown;
        image->m_viewDescriptor.m_overrideFormat    = format;
        image->m_viewDescriptor.m_overrideBindFlags =
            writes ? RHI::ImageBindFlags::ShaderReadWrite : RHI::ImageBindFlags::ShaderRead;
        return *this;
    }

    ShaderAttachment& ShaderAttachment::Bind(const RHI::InputName& input)
    {
        m_builder->BindShaderInput(GetHandle(), input);
        return *this;
    }

    ShaderAttachment& ShaderAttachment::BindIndex(const RHI::InputName& input)
    {
        m_builder->BindShaderInputIndex(GetHandle(), input);
        return *this;
    }

    bool ShaderAttachment::IsPreviousFrameMissing() const
    {
        const auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<PreviousFrameTag>(GetHandle()), "Not a ReadPrevious access.");
        return rhiContext.Has<PreviousFrameMissingTag>(GetHandle());
    }

    ShaderAttachment& ShaderAttachment::Stage(RHI::AttachmentStage stage)
    {
        ASSERT(!m_fixedStage, "A compute pass's shader accesses are always in the compute stage.");
        auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle handle = m_attachment.GetHandle();
        if (auto* image = rhiContext.TryGet<ImagePassAttachment>(handle))
        {
            image->m_stage = stage;
        }
        else
        {
            rhiContext.Get<BufferPassAttachment>(handle).m_stage = stage;
        }
        return *this;
    }

    // ============================================================
    // RenderScope
    // ============================================================

    Attachment RenderScope::RenderTarget(const RHI::AttachmentId& name, const RHI::AttachmentLoadStoreAction& action)
    {
        return Attachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::RenderTarget, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ColorAttachmentOutput, &action));
    }

    Attachment RenderScope::DepthWrite(const RHI::AttachmentId& name, const RHI::AttachmentLoadStoreAction& action)
    {
        return Attachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::DepthStencil, RHI::AttachmentAccess::Write, DepthStages, &action));
    }

    Attachment RenderScope::DepthRead(const RHI::AttachmentId& name)
    {
        const RHI::AttachmentLoadStoreAction action;
        return Attachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::DepthStencil, RHI::AttachmentAccess::Read, DepthStages, &action));
    }

    Attachment RenderScope::Resolve(const RHI::AttachmentId& name, const Attachment& source)
    {
        auto& rhiContext = *RHIExecuteContext::Current();
        ASSERT(rhiContext.Has<ColorAttachmentIndex>(source.GetHandle())
                && rhiContext.Get<ScopeAttachment>(source.GetHandle()).m_scope == m_scope,
            "A Resolve's source must be a RenderTarget of the same Scope.");

        // The resolve writes every pixel, so nothing is loaded.
        RHI::AttachmentLoadStoreAction action;
        action.m_loadAction  = RHI::AttachmentLoadAction::DontCare;
        action.m_storeAction = RHI::AttachmentStoreAction::Store;
        const RHIHandle handle = m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Resolve, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ColorAttachmentOutput, &action);
        rhiContext.Add<ResolveSource>(handle, ResolveSource{ source.GetHandle() });
        return Attachment(handle);
    }

    ShaderAttachment RenderScope::Read(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read,
            RHI::AttachmentStage::Uninitialized, nullptr), false);
    }

    ShaderAttachment RenderScope::ReadWrite(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite,
            RHI::AttachmentStage::Uninitialized, nullptr), false);
    }

    void RenderScope::Draw(const RHI::DrawArguments& arguments, uint32_t instanceCount)
    {
        RHI::DrawItem item;
        item.m_drawArguments    = arguments;
        item.m_drawInstanceArgs = RHI::DrawInstanceArguments(instanceCount, 0);
        RHIExecuteContext::Current()->Add<RHI::DrawItem>(m_builder->AddScopeItem(m_scope), item);
    }

    ShaderAttachment RenderScope::ReadPrevious(const RHI::AttachmentId& name)
    {
        ImagePassAttachment a;
        a.m_attachmentId = AttachmentId{ name, 0, 1 };
        a.m_usage        = RHI::AttachmentUsage::Shader;
        a.m_stage        = RHI::AttachmentStage::Uninitialized;
        return ShaderAttachment(*m_builder, m_builder->AddPreviousFrameAttachment(a, m_scope), false);
    }

    // ============================================================
    // ComputeScope
    // ============================================================

    ShaderAttachment ComputeScope::ReadPrevious(const RHI::AttachmentId& name)
    {
        ImagePassAttachment a;
        a.m_attachmentId = AttachmentId{ name, 0, 1 };
        a.m_usage        = RHI::AttachmentUsage::Shader;
        a.m_stage        = RHI::AttachmentStage::ComputeShader;
        return ShaderAttachment(*m_builder, m_builder->AddPreviousFrameAttachment(a, m_scope), true);
    }

    ShaderAttachment ComputeScope::Read(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }

    ShaderAttachment ComputeScope::ReadWrite(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }

    ShaderAttachment ComputeScope::Write(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(*m_builder, m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }
}
