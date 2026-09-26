#pragma once

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Command/DrawArguments.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Format.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <EASTL/type_traits.h>
#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>

#include "RenderGraphBuilder.h"

namespace Spark::Render
{
    //! A ScopeSelections::Collect: the live scene DrawItems carrying all of DrawTags. An item
    //! marked dead this frame is left out, though its entity stays valid through recording.
    template<typename... DrawTags>
    void CollectDrawItems(RHI::RHIContext& context, RHI::RHIHandle /*view*/, eastl::vector<RHI::RHIHandle>& submitList)
    {
        context.GetView<RHI::DrawItem, DrawTags...>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle item, const RHI::DrawItem&) { submitList.push_back(item); });
    }

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

    //! An attachment a shader accesses. In a render pass it needs a stage: from .Bind, which
    //! takes the stages of the input it binds to, or from .Stage.
    class ShaderAttachment
    {
    public:
        //! Reinterpret the image in another format, viewed for shader access only (a depth
        //! image read as R32_FLOAT gets no depth-stencil view).
        ShaderAttachment& Format(RHI::Format format);

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

        //! The shader stages that access it, for an access no .Bind names (.BindIndex, or read
        //! through a shared binding). Render passes only: a compute pass's is fixed.
        ShaderAttachment& Stage(RHI::AttachmentStage stage);

        //! Bind it to the per-pass shader input `input`: lowering puts its view there.
        ShaderAttachment& Bind(const RHI::InputName& input);

        //! Bind it by heap index to the uint constant `input`, a root constant or a per-pass
        //! one: lowering writes its view's bindless index there. Beside .Bind, not instead of
        //! it; an access is bound one way. In a render pass it needs .Stage.
        ShaderAttachment& BindIndex(const RHI::InputName& input);

        //! For a ReadPrevious access: whether last frame left no copy, so it reads a stand-in.
        bool IsPreviousFrameMissing() const;

        RHIHandle GetHandle() const { return m_attachment.GetHandle(); }

    private:
        friend class RenderScope;
        friend class ComputeScope;

        ShaderAttachment(RenderGraphBuilder& builder, RHIHandle handle, bool fixedStage)
            : m_builder(&builder)
            , m_attachment(handle)
            , m_fixedStage(fixedStage)
        {
        }

        RenderGraphBuilder* m_builder { nullptr };
        Attachment          m_attachment;
        bool                m_fixedStage { false };
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

        //! Resolve the multisampled render target `source`, declared in this Scope, into `name`
        //! when the render pass ends.
        Attachment Resolve(const RHI::AttachmentId& name, const Attachment& source);

        ShaderAttachment Read(const RHI::AttachmentId& name);
        ShaderAttachment ReadWrite(const RHI::AttachmentId& name);

        //! A draw of this Scope that reads no vertex or index buffer (vertices come from
        //! SV_VertexID), e.g. a full-screen triangle: DrawLinear(3, 0).
        void Draw(const RHI::DrawArguments& arguments, uint32_t instanceCount = 1);

        //! Draw the scene's items that carry all of DrawTags, e.g. Accepts<OpaqueTag>().
        template<typename... DrawTags>
        void Accepts()
        {
            m_builder->AddScopeSelection(m_scope, &CollectDrawItems<DrawTags...>);
        }

        //! Read the copy of `name` produced last frame (see ShaderAttachment::IsPreviousFrameMissing).
        ShaderAttachment ReadPrevious(const RHI::AttachmentId& name);

        //! Set the per-pass sampler / constant `input` for this Scope. Scopes of one pass that
        //! set the same input must agree: the per-pass space holds one value.
        void Sampler(const RHI::InputName& input, const RHI::SamplerState& state)
        {
            m_builder->AddScopeSampler(m_scope, input, state);
        }

        template<typename T>
        void Constant(const RHI::InputName& input, const T& value)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "A constant is copied as bytes.");
            m_builder->AddScopeConstant(m_scope, input, &value, static_cast<uint32_t>(sizeof(T)));
        }

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

        //! Read the copy of `name` produced last frame (see ShaderAttachment::IsPreviousFrameMissing).
        ShaderAttachment ReadPrevious(const RHI::AttachmentId& name);

        //! A dispatch covering this many threads, not groups: the group count follows from the
        //! shader's [numthreads], rounded up, so the shader must bounds-check the excess.
        void Dispatch(uint32_t threadCountX, uint32_t threadCountY = 1, uint32_t threadCountZ = 1)
        {
            m_builder->AddScopeDispatch(m_scope, threadCountX, threadCountY, threadCountZ);
        }

        //! Set the per-pass sampler / constant `input` for this Scope. Scopes of one pass that
        //! set the same input must agree: the per-pass space holds one value.
        void Sampler(const RHI::InputName& input, const RHI::SamplerState& state)
        {
            m_builder->AddScopeSampler(m_scope, input, state);
        }

        template<typename T>
        void Constant(const RHI::InputName& input, const T& value)
        {
            static_assert(eastl::is_trivially_copyable_v<T>, "A constant is copied as bytes.");
            m_builder->AddScopeConstant(m_scope, input, &value, static_cast<uint32_t>(sizeof(T)));
        }

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

        //! Introduce `resource`, owned outside the graph, under `name`.
        void Import(const RHI::AttachmentId& name, RHIHandle resource)
        {
            m_builder.ImportResource(name, resource);
        }

        RHIHandle GetCurrentSwapChainResource() const { return m_builder.GetCurrentSwapChainResource(); }

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

        //! Introduce `resource`, owned outside the graph, under `name`.
        void Import(const RHI::AttachmentId& name, RHIHandle resource)
        {
            m_builder.ImportResource(name, resource);
        }

        RHIHandle GetCurrentSwapChainResource() const { return m_builder.GetCurrentSwapChainResource(); }

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
