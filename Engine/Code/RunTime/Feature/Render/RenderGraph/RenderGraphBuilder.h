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

        void AddEdge(Pass from, Pass to);

        void TouchNode(Pass pass);

        void BuildGraph();

        eastl::vector<Pass> TopoSort();

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

        //! Open a new Scope of the current pass, numbered after those it already has.
        RHIHandle OpenScope();

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

        //! A new item of `scope`: an entity carrying ScopeItem, for the caller to give its
        //! DrawItem. It lives for the frame.
        RHIHandle AddScopeItem(RHIHandle scope);

        //! A set of scene items `scope` selects: `collect` appends its members under a view.
        void AddScopeSelection(RHIHandle scope, ScopeSelections::Collect collect);

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
        // use. No validation.
        RHIHandle AddImageAttachment(const ImagePassAttachment& attachment, RHIHandle scope, uint32_t* colorCount);
        RHIHandle AddBufferAttachment(const BufferPassAttachment& attachment, RHIHandle scope);

        void CountScopeAttachment(RHIHandle scope);

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
