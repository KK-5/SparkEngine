#pragma once

#include <EASTL/fixed_vector.h>
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
#include "RenderGraphResolve.h"

namespace Spark::RHI
{
    class ImagePool;
}

namespace Spark::Render
{
    class RenderGraphBuilder
    {
    public:
        RenderGraphBuilder() = default;
        ~RenderGraphBuilder() = default;

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

        void Begin(uint32_t frameIndex, RHI::RHIHandle swapChainResource,
                   const Math::Vector2Int& renderSize, const Math::Vector2Int& outputSize);

        void End();

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

        //! Open a new Scope of the current pass, numbered after those it already has. The one
        //! before it must be closed: a pass has one Scope open at a time, and things are added
        //! only to that one.
        RHIHandle OpenScope();

        //! End the declaration of the open Scope and validate what it declared.
        void CloseScope(RHIHandle scope);

        //! Introduce a transient resource under `name`, with no access yet. Accesses of the
        //! name may have been declared already, by this pass or an earlier one.
        void CreateImage(const RHI::AttachmentId& name, const RHI::ImageDescriptor& desc);
        void CreateBuffer(const RHI::AttachmentId& name, const RHI::BufferDescriptor& desc);

        //! Introduce `resource`, owned outside the graph, under `name`, with no access yet.
        //! Importing a name again is fine for the same resource only.
        void ImportResource(const RHI::AttachmentId& name, RHIHandle resource);

        //! Add to `scope` a read of the copy of `attachment`'s name produced last frame. The
        //! caller fills the attachment's id, usage, stage, action and view. End links it to
        //! that copy, a pooled image, or to a stand-in when there is none, and marks this
        //! frame's resource for extraction, so the next frame can read it back.
        RHIHandle AddPreviousFrameAttachment(ImagePassAttachment attachment, RHIHandle scope);

        //! Bind to the uint constant `input`, a root constant or a per-pass one, whether the
        //! previous-frame read `attachment` gets last frame's content: lowering writes 1 there,
        //! or 0 when it reads a stand-in.
        void BindPreviousFrameValid(RHIHandle attachment, const RHI::InputName& input);

        //! Bind `attachment` to the shader input `input` of the current pass's per-pass space:
        //! checks the input exists there and suits the access (SRV for reads, UAV for
        //! writes), and gives the attachment the input's stages, or checks them against its
        //! own.
        void BindShaderInput(RHIHandle attachment, const RHI::InputName& input);

        //! Bind `attachment` by heap index to the uint constant `input`, a root constant or a
        //! per-pass one: lowering writes its view's bindless index there. The access needs a
        //! stage from elsewhere (.Stage, or a compute pass's).
        void BindShaderInputIndex(RHIHandle attachment, const RHI::InputName& input);

        //! A sampler `scope` sets in the current pass's per-pass space.
        void AddScopeSampler(RHIHandle scope, const RHI::InputName& input, const RHI::SamplerState& state);

        //! A constant `scope` sets: a root constant if the pass's shaders declare one called
        //! `input`, written into the Scope's block now, else one of its per-pass space.
        void AddScopeConstant(RHIHandle scope, const RHI::InputName& input, const void* bytes, uint32_t byteCount);

        //! A new item of `scope`: an entity carrying ScopeItem, for the caller to give its
        //! DrawItem. It lives for the frame.
        RHIHandle AddScopeItem(RHIHandle scope);

        //! A dispatch of `scope` covering the given thread counts: the groups follow from the
        //! current pass's [numthreads] (PassThreadGroupSize), rounded up.
        void AddScopeDispatch(RHIHandle scope, uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ);

        //! A set of scene items `scope` selects: `collect` appends its members under a view.
        void AddScopeSelection(RHIHandle scope, ScopeSelections::Collect collect);

        //! The current pass's pipeline layout, reflected from its shaders.
        const RHI::PipelineLayoutDescriptor& CurrentPassLayout() const;

        //! Add to `scope` an access of the image called `name`; End gives it its version. A
        //! name nothing has declared yet gets its attachment marked (UnlinkedAttachmentTag)
        //! for End to link. The first clearing access of a transient image gives the resource
        //! its clear value. `colorCount` numbers the Scope's render targets (null outside a
        //! render pass); `action` is for render targets and depth only.
        RHIHandle AddScopeAttachment(
            RHIHandle scope, uint32_t* colorCount, const RHI::AttachmentId& name,
            RHI::AttachmentUsage usage, RHI::AttachmentAccess access, RHI::AttachmentStage stage,
            const RHI::AttachmentLoadStoreAction* action);

        //! The same for the buffer called `name`.
        RHIHandle AddScopeBufferAttachment(
            RHIHandle scope, const RHI::AttachmentId& name,
            RHI::AttachmentUsage usage, RHI::AttachmentAccess access, RHI::AttachmentStage stage);

        //! The resource declared under `name` so far, or NullHandle.
        RHIHandle FindResource(const RHI::AttachmentId& name) const;

        // Pure registration into `scope`: build attachment entity, attach components. No
        // validation.
        RHIHandle AddImageAttachment(const ImagePassAttachment& attachment, RHIHandle scope, uint32_t* colorCount);
        RHIHandle AddBufferAttachment(const BufferPassAttachment& attachment, RHIHandle scope);

        //! Asserts that `scope` is the one open.
        void CheckScopeOpen(RHIHandle scope) const;

        // The transient image declared under `name` so far, or NullHandle. Used to link a
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

        //! End's steps, before ResolveGraph. The first links the attachments declared ahead of
        //! their resource (UnlinkedAttachmentTag) to it, now that every pass has declared its
        //! own. The second links the previous-frame reads to last frame's copies, after it: a
        //! stand-in takes the clear value this frame's resource was given. One that reads a
        //! stand-in is marked (PreviousFrameMissingTag).
        void LinkAttachments();
        void LinkPreviousFrameReads();

        //! Asserts on what ResolveGraph found wrong.
        void CheckResolution() const;

        //! Checks that the current pass's shaders have a 4-byte constant `input` a Scope can
        //! set, a root constant or a per-pass one, for lowering to write a value an attachment
        //! gives it; a root constant is marked set in `scope`.
        void ReserveUintConstant(RHIHandle scope, const RHI::InputName& input);

        Pass m_currentPass {NullPass};

        //! How many Scopes the current pass has opened.
        uint32_t m_passScopeCount {0};

        //! The Scope being declared, NullHandle between Scopes, and its attachments: what
        //! CloseScope validates.
        RHIHandle m_openScope {NullHandle};
        eastl::fixed_vector<RHIHandle, 8> m_scopeAttachments;

        //! The frame's attachments, in the order they were declared.
        eastl::vector<RHIHandle> m_attachments;

        //! The resource Create / Import put under each name.
        eastl::unordered_map<RHI::AttachmentId, RHIHandle> m_resources;

        GraphResolution m_resolution;

        uint32_t m_frameIndex { 0 };

        // Frame-scoped inputs seeded by Begin, not state.
        Math::Vector2Int m_renderSize { 0, 0 };
        Math::Vector2Int m_outputSize { 0, 0 };

        RHI::RHIHandle m_curSwapChainResource;

        // Owned by RenderGraph; stand-ins for a missing previous frame come from here.
        RHI::ImagePool* m_imagePool { nullptr };
    };

    // ============================================================
    // Registration helpers
    // ============================================================

    inline RHIHandle RenderGraphBuilder::FindTransientImage(const RHI::AttachmentId& name) const
    {
        auto it = m_resources.find(name);
        if (it == m_resources.end())
        {
            return NullHandle;
        }
        const auto& rhiContext = *RHIExecuteContext::Current();
        const RHIHandle resource = it->second;
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
}
