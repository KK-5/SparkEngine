#include "PassScopes.h"

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

    ShaderAttachment RenderScope::Read(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read,
            RHI::AttachmentStage::Uninitialized, nullptr), false);
    }

    ShaderAttachment RenderScope::ReadWrite(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(m_builder->AddScopeAttachment(m_scope, &m_colorCount, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite,
            RHI::AttachmentStage::Uninitialized, nullptr), false);
    }

    // ============================================================
    // ComputeScope
    // ============================================================

    ShaderAttachment ComputeScope::Read(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }

    ShaderAttachment ComputeScope::ReadWrite(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::ReadWrite,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }

    ShaderAttachment ComputeScope::Write(const RHI::AttachmentId& name)
    {
        return ShaderAttachment(m_builder->AddScopeAttachment(m_scope, nullptr, name,
            RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Write,
            RHI::AttachmentStage::ComputeShader, nullptr), true);
    }
}
