#pragma once

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/RHILimits.h>

#include <Pass/Pass.h>
#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>

#include <Shader/ShaderBindingsUtils.h>
#include <CoreComponents/Tags.h>
#include <EASTL/vector.h>

namespace Spark::Render
{
    //! Lookup the pass entity carrying the given compile-time PassTag.
    //! Asserts exactly one match — duplicate registration or a 32-bit FNV-1a
    //! hash collision is caught here at the call site, not silently in
    //! downstream component access.
    //!
    //! Intended for init-time use; callers should cache the returned handle
    //! rather than re-look-up per frame.
    template<typename PassTagT>
    Pass FindPass(PassContext& ctx)
    {
        auto view = ctx.GetView<PassTagT>();
        ASSERT(view.size() == 1,
            "FindPass: expected exactly 1 pass with the given PassTag, got {}.",
            static_cast<uint32_t>(view.size()));

        Pass result = NullPass;
        view.each([&](Pass p) { result = p; });
        return result;
    }

    namespace Detail
    {
    //! Allocate a ShaderBindings against the named pass's input layout and
    //! register it on the RHIContext so RenderGraphCompiler::CompileShaderInputs
    //! can find it. The entity is created with ShaderBindingsUpdateTag so the very
    //! first compile sweep picks it up and produces valid GPU bindings before the
    //! first execute.
    //!
    //! Returns the binding ENTITY (RHIHandle) only — not the RHI::ShaderBindings
    //! Ptr. Stage data through the entity with the Render::SetShaderXxx helpers
    //! (Shader/ShaderBindingsUtils.h), which also mark it dirty. The entity owns the
    //! binding's lifetime (Components::ShaderBindings holds the Ptr), so destroying the
    //! entity releases it.
    //!
    //! Returns NullHandle if the pass has no PassPipelineLayout (e.g.
    //! custom-pipeline pass), or if RHI services / Init fail.
    template<typename PassTagT>
    RHIHandle CreatePassShaderBindings(
        PassContext& passCtx, RHIContext& rhiCtx, uint32_t spaceId)
    {
        Pass pass = FindPass<PassTagT>(passCtx);

        if (!passCtx.Has<PassPipelineLayout>(pass))
        {
            LOG_ERROR("[CreatePassShaderBindings] Pass has no PassPipelineLayout "
                      "(custom-pipeline pass or shader reflection unavailable).");
            return NullHandle;
        }
        auto& layout = passCtx.Get<PassPipelineLayout>(pass).m_layout;
        ASSERT(layout, "[CreatePassShaderBindings] PassPipelineLayout component holds null layout.");

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[CreatePassShaderBindings] RHI::RHIInterface service not registered.");

        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device,
            "[CreatePassShaderBindings] RHI factory or device is null.");

        Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = spaceId;

        const RHI::ResultCode rc = bindings->Init(*device, desc);
        if (rc != RHI::ResultCode::Success)
        {
            LOG_ERROR("[CreatePassShaderBindings] ShaderBindings::Init failed for spaceId={}.", spaceId);
            return NullHandle;
        }

        // Register on RHIContext for CompileShaderInputs discovery; flag as
        // dirty so the very first frame compiles the bindings before execute.
        // The entity owns the Ptr from here on.
        RHIHandle entity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(entity, RHI::Components::ShaderBindings{ bindings });
        rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(entity);

        return entity;
    }
    } // namespace Detail

    //! Get-or-create the per-pass ShaderBindings identified by (PassTag, spaceId),
    //! lazily creating it on first use. Wraps CreatePassShaderBindings but adds
    //! ownership + reuse: the SRG entity is stamped with PassTag so it can be found
    //! again on later frames, and an existing (PassTag, spaceId) entity is returned
    //! as-is instead of re-created.
    //!
    //! This is the binding-access path for SAMPLING passes: a pass that samples an
    //! upstream (often transient) attachment resolves its view during Compile
    //! (FindPassAttachmentImageView) — a phase with no owning object to hold the SRG
    //! handle, so the SRG is located by tag + space here rather than captured.
    //! CreatePassShaderBindings, by contrast, stays a pure create for callers that
    //! own and store the handle themselves; its contract is left unchanged. The get-or-
    //! create form is what RenderPassBuilder::Finalize calls to auto-allocate a pass's
    //! space2 SRG.
    //!
    //! Uniqueness leans on ShaderBindings::GetSpaceId self-describing the HLSL space,
    //! so no extra component is needed to disambiguate multiple per-pass SRGs. The
    //! (PassTag, Components::ShaderBindings) view never collides with attachment
    //! entities (which carry PassTag but no ShaderBindings). Returns NullHandle if
    //! creation fails (e.g. a custom-pipeline pass has no PassPipelineLayout).
    template<typename PassTagT>
    RHIHandle GetOrCreatePassShaderBindings(
        PassContext& passCtx, RHIContext& rhiCtx, uint32_t spaceId)
    {
        // Reuse: an SRG already owned by this pass at the requested space.
        for (auto [entity, comp] : rhiCtx.GetView<PassTagT, RHI::Components::ShaderBindings>().each())
        {
            if (comp.m_bindings && comp.m_bindings->GetSpaceId() == spaceId)
            {
                return entity;
            }
        }

        // Miss: delegate the create (Detail:: — the untagged create is internal now),
        // then stamp PassTag (lookup key) + PassShaderBindingsTag (teardown handle).
        RHIHandle entity = Detail::CreatePassShaderBindings<PassTagT>(passCtx, rhiCtx, spaceId);
        if (entity != NullHandle)
        {
            rhiCtx.Add<PassTagT>(entity);
            rhiCtx.Add<PassShaderBindingsTag>(entity);
        }
        return entity;
    }

    // ============================================================
    // Per-pass shader-binding data injection. Fully encapsulated: callers never touch
    // the entity — the (PassTag, spaceId) bindings are get-or-created lazily and the
    // value is staged into them. Returns false only when they can't be created yet
    // (pass layout not reflected), letting callers gate readiness. ResolvePassSharedBindings
    // picks them up by PassTag, completing the per-pass binding path.
    // ============================================================

    template<typename PassTag>
    bool SetPassShaderImage(uint32_t spaceId, RHI::InputName input, const RHI::ImageView* view, uint32_t arrayIndex = 0)
    {
        RHIHandle srg = GetOrCreatePassShaderBindings<PassTag>(
            *PassExecuteContext::Current(), *RHIExecuteContext::Current(), spaceId);
        if (srg == NullHandle)
        {
            return false;
        }
        SetShaderImage(srg, input, view, arrayIndex);
        return true;
    }

    template<typename PassTag>
    bool SetPassShaderSampler(uint32_t spaceId, RHI::InputName input, const RHI::SamplerState& state, uint32_t arrayIndex = 0)
    {
        RHIHandle srg = GetOrCreatePassShaderBindings<PassTag>(
            *PassExecuteContext::Current(), *RHIExecuteContext::Current(), spaceId);
        if (srg == NullHandle)
        {
            return false;
        }
        SetShaderSampler(srg, input, state, arrayIndex);
        return true;
    }

    template<typename PassTag>
    bool SetPassShaderBuffer(uint32_t spaceId, RHI::InputName input, const RHI::BufferView* view, uint32_t arrayIndex = 0)
    {
        RHIHandle srg = GetOrCreatePassShaderBindings<PassTag>(
            *PassExecuteContext::Current(), *RHIExecuteContext::Current(), spaceId);
        if (srg == NullHandle)
        {
            return false;
        }
        SetShaderBuffer(srg, input, view, arrayIndex);
        return true;
    }

    template<typename PassTag, typename T>
    bool SetPassShaderConstant(uint32_t spaceId, RHI::InputName input, const T& value)
    {
        RHIHandle srg = GetOrCreatePassShaderBindings<PassTag>(
            *PassExecuteContext::Current(), *RHIExecuteContext::Current(), spaceId);
        if (srg == NullHandle)
        {
            return false;
        }
        SetShaderConstant(srg, input, value);
        return true;
    }

    //! Reap every per-pass ShaderBindings (created via GetOrCreatePassShaderBindings)
    //! by its runtime PassShaderBindingsTag. Call once at pipeline / RenderSystem
    //! teardown — per-pass SRGs are persistent and have no external owner, so this is
    //! their single collection point, independent of the compile-time PassTag. Uses
    //! DeadTag (same path as the view / instance SRGs) so the reap is uniform.
    inline void ReapPassShaderBindings(RHIContext& ctx)
    {
        eastl::vector<RHIHandle> dead;
        for (auto [entity, comp] : ctx.GetView<PassShaderBindingsTag, RHI::Components::ShaderBindings>().each())
        {
            dead.push_back(entity);
        }
        for (RHIHandle entity : dead)
        {
            ctx.Add<DeadTag>(entity);
        }
    }

    //! Mark a ShaderBindings entity as dirty so RenderGraphCompiler::CompileShaderInputs
    //! will recompile it before the next execute. Call after SetBuffer / SetImage /
    //! SetSampler / SetConstant changes. Idempotent: re-tagging a still-dirty
    //! entity within a single frame is a no-op.
    inline void MarkShaderBindingsUpdate(RHIContext& rhiCtx, RHIHandle entity)
    {
        if (entity == NullHandle)
        {
            return;
        }
        if (!rhiCtx.Has<RHI::ShaderBindingsUpdateTag>(entity))
        {
            rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(entity);
        }
    }

    //! Resolve a declared image attachment to its backing RHI::Image via the
    //! pass→resource edge (the ImagePassAttachment entity tagged with PassTag).
    //! Reads attachment.m_image (the resource entity) → BackingImage directly —
    //! one hop, no view indirection. Valid once the resource is materialized
    //! (transient: after CompileTransientResources; imported/static: at import).
    template<typename PassTag>
    RHI::Image* FindPassAttachmentImage(RHIContext& rhiCtx, RHI::InputName slot)
    {
        RHI::Image* result = nullptr;
        for (auto [handle, attachment] : rhiCtx.GetView<PassTag, ImagePassAttachment>().each())
        {
            if (attachment.m_slotName != slot)
            {
                continue;
            }

            auto* backImage = rhiCtx.TryGet<BackingImage>(attachment.m_image);
            if (!backImage)
            {
                LOG_ERROR("[FindPassAttachmentImage] Image entity has no BackingImage component (slot: {}).",
                    slot.GetCStr());
                break;
            }

            result = backImage->m_image;
            break;
        }

        return result;
    }

    //! Resolve a declared image attachment to an RHI::ImageView via the pass→resource
    //! edge (the ImagePassAttachment entity tagged with PassTag). Frame-aware:
    //!  - Per-frame resource (PerFrameTag — swap chain / ImagePerFrame): the current
    //!    frame's view from ImageViewCachePerFrame (frameIndex selects the slot).
    //!  - Single-frame resource: the stable view from ImageViewCache (frameIndex unused).
    //! The view is deduplicated and reused, not minted fresh per call. Valid once the
    //! resource is materialized (transient: after CompileTransientResources; imported:
    //! at import).
    //!
    //! frameIndex is typically RenderGraphExecuter::GetFrameIndex() at execute time.
    //! Returns the view (e.g. to feed RHI::ShaderBindings::SetImage); nullptr if the
    //! slot is not found, the resource is unmaterialized, or view Init fails.
    template<typename PassTag>
    RHI::ImageView* FindPassAttachmentImageView(RHIContext& rhiCtx, RHI::InputName slot, uint32_t frameIndex)
    {
        RHI::ImageView* result = nullptr;
        for (auto [handle, attachment] : rhiCtx.GetView<PassTag, ImagePassAttachment>().each())
        {
            if (attachment.m_slotName != slot)
            {
                continue;
            }

            auto* backImage = rhiCtx.TryGet<BackingImage>(attachment.m_image);
            if (!backImage || !backImage->m_image)
            {
                LOG_ERROR("[FindPassAttachmentImageView] Image entity has no backing image (slot: {}).",
                    slot.GetCStr());
                break;
            }

            if (rhiCtx.Has<RHI::PerFrameTag>(attachment.m_image))
            {
                result = RHI::GetOrCreateImageViewPerFrame(
                    rhiCtx, attachment.m_image, *backImage->m_image, attachment.m_viewDescriptor, frameIndex);
            }
            else
            {
                result = RHI::GetOrCreateImageView(
                    rhiCtx, attachment.m_image, *backImage->m_image, attachment.m_viewDescriptor);
            }
            break;
        }

        return result;
    }

    template<typename PassTag>
    RHI::Buffer* FindPassAttachmentBuffer(RHIContext& rhiCtx, RHI::InputName slot)
    {
        RHI::Buffer* result = nullptr;
        for (auto [handle, attachment] : rhiCtx.GetView<PassTag, BufferPassAttachment>().each())
        {
            if (attachment.m_slotName != slot)
            {
                continue;
            }

            auto* backBuffer = rhiCtx.TryGet<BackingBuffer>(attachment.m_buffer);
            if (!backBuffer)
            {
                LOG_ERROR("[FindPassAttachmentBuffer] Buffer entity has no BackingBuffer component (slot: {}).",
                    slot.GetCStr());
                break;
            }

            result = backBuffer->m_buffer;
            break;
        }

        return result;
    }
}
