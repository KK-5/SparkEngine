#pragma once

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Format.h>

#include "RenderGraphBuilder.h"

namespace Spark::Render
{
    //! An attachment a Scope just declared. Refines the view it is accessed through.
    class Attachment
    {
    public:
        //! Reinterpret the image in another format (e.g. a depth image read as R32_FLOAT).
        Attachment& Format(RHI::Format format);
        Attachment& View(const RHI::ImageViewDescriptor& view);
        Attachment& View(const RHI::BufferViewDescriptor& view);

        RHIHandle GetHandle() const { return m_handle; }

    private:
        friend class RenderScope;
        friend class ComputeScope;
        friend class ShaderAttachment;

        explicit Attachment(RHIHandle handle)
            : m_handle(handle)
        {
        }

        RHIHandle m_handle { NullHandle };
    };

    //! An attachment a shader accesses. In a render pass it needs a stage, from .Stage.
    class ShaderAttachment
    {
    public:
        ShaderAttachment& Format(RHI::Format format)
        {
            m_attachment.Format(format);
            return *this;
        }

        ShaderAttachment& View(const RHI::ImageViewDescriptor& view)
        {
            m_attachment.View(view);
            return *this;
        }

        ShaderAttachment& View(const RHI::BufferViewDescriptor& view)
        {
            m_attachment.View(view);
            return *this;
        }

        //! The shader stages that access it. Render passes only: a compute pass's is fixed.
        ShaderAttachment& Stage(RHI::AttachmentStage stage);

        RHIHandle GetHandle() const { return m_attachment.GetHandle(); }

    private:
        friend class RenderScope;
        friend class ComputeScope;

        ShaderAttachment(RHIHandle handle, bool fixedStage)
            : m_attachment(handle)
            , m_fixedStage(fixedStage)
        {
        }

        Attachment m_attachment;
        bool       m_fixedStage { false };
    };

    //! One Scope of a render pass: one render pass bracket.
    class RenderScope
    {
    public:
        RenderScope(RenderScope&&) = default;
        RenderScope(const RenderScope&) = delete;
        RenderScope& operator=(const RenderScope&) = delete;

        //! Color targets are numbered in the order they are declared.
        Attachment RenderTarget(
            const RHI::AttachmentId& name, const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction());
        Attachment DepthWrite(
            const RHI::AttachmentId& name, const RHI::AttachmentLoadStoreAction& action = RHI::AttachmentLoadStoreAction());
        Attachment DepthRead(const RHI::AttachmentId& name);

        ShaderAttachment Read(const RHI::AttachmentId& name);
        ShaderAttachment ReadWrite(const RHI::AttachmentId& name);

    private:
        friend class RenderPassScopes;

        RenderScope(RenderGraphBuilder& builder, RHIHandle scope)
            : m_builder(&builder)
            , m_scope(scope)
        {
        }

        RenderGraphBuilder* m_builder { nullptr };
        RHIHandle           m_scope { NullHandle };
        uint32_t            m_colorCount { 0 };
    };

    //! One Scope of a compute pass.
    class ComputeScope
    {
    public:
        ComputeScope(ComputeScope&&) = default;
        ComputeScope(const ComputeScope&) = delete;
        ComputeScope& operator=(const ComputeScope&) = delete;

        ShaderAttachment Read(const RHI::AttachmentId& name);
        ShaderAttachment ReadWrite(const RHI::AttachmentId& name);
        ShaderAttachment Write(const RHI::AttachmentId& name);

    private:
        friend class ComputePassScopes;

        ComputeScope(RenderGraphBuilder& builder, RHIHandle scope)
            : m_builder(&builder)
            , m_scope(scope)
        {
        }

        RenderGraphBuilder* m_builder { nullptr };
        RHIHandle           m_scope { NullHandle };
    };

    //! What a render pass's Build callback declares its frame with: the resources it
    //! introduces and the Scopes it runs as. Declaring nothing skips the pass this frame.
    class RenderPassScopes
    {
    public:
        explicit RenderPassScopes(RenderGraphBuilder& builder)
            : m_builder(builder)
        {
        }

        void CreateImage(const RHI::AttachmentId& name, const RHI::ImageDescriptor& desc)
        {
            m_builder.CreateImage(name, desc);
        }

        void CreateBuffer(const RHI::AttachmentId& name, const RHI::BufferDescriptor& desc)
        {
            m_builder.CreateBuffer(name, desc);
        }

        RenderScope Scope()
        {
            return RenderScope(m_builder, m_builder.OpenScope());
        }

        uint32_t         GetFrameIndex() const { return m_builder.GetFrameIndex(); }
        Math::Vector2Int GetRenderSize() const { return m_builder.GetRenderSize(); }
        Math::Vector2Int GetOutputSize() const { return m_builder.GetOutputSize(); }

    private:
        RenderGraphBuilder& m_builder;
    };

    //! The compute counterpart of RenderPassScopes.
    class ComputePassScopes
    {
    public:
        explicit ComputePassScopes(RenderGraphBuilder& builder)
            : m_builder(builder)
        {
        }

        void CreateImage(const RHI::AttachmentId& name, const RHI::ImageDescriptor& desc)
        {
            m_builder.CreateImage(name, desc);
        }

        void CreateBuffer(const RHI::AttachmentId& name, const RHI::BufferDescriptor& desc)
        {
            m_builder.CreateBuffer(name, desc);
        }

        ComputeScope Scope()
        {
            return ComputeScope(m_builder, m_builder.OpenScope());
        }

        uint32_t         GetFrameIndex() const { return m_builder.GetFrameIndex(); }
        Math::Vector2Int GetRenderSize() const { return m_builder.GetRenderSize(); }
        Math::Vector2Int GetOutputSize() const { return m_builder.GetOutputSize(); }

    private:
        RenderGraphBuilder& m_builder;
    };
}
