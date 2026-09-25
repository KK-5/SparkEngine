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
    //! The PassBindings entity of the pass carrying PassTag, or NullHandle if it has none.
    template<typename PassTag>
    RHIHandle FindPassBindings(uint32_t spaceId)
    {
        ASSERT(spaceId == kPerPassSpaceId, "[SetPassShader] Only the per-pass space ({}) is supported, got {}.",
            kPerPassSpaceId, spaceId);
        PassContext& passCtx = *PassExecuteContext::Current();
        const auto*  own     = passCtx.TryGet<PassBindings>(FindPass<PassTag>(passCtx));
        return own != nullptr ? own->m_bindings : NullHandle;
    }
    } // namespace Detail

    // ============================================================
    // Per-pass shader-binding data injection into the pass's PassBindings. Returns false
    // only when the pass has none (its layout declares no per-pass space), letting callers
    // gate readiness. CompileScopeState binds them from PassBindings.
    // ============================================================

    template<typename PassTag>
    bool SetPassShaderImage(uint32_t spaceId, RHI::InputName input, const RHI::ImageView* view, uint32_t arrayIndex = 0)
    {
        RHIHandle srg = Detail::FindPassBindings<PassTag>(spaceId);
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
        RHIHandle srg = Detail::FindPassBindings<PassTag>(spaceId);
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
        RHIHandle srg = Detail::FindPassBindings<PassTag>(spaceId);
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
        RHIHandle srg = Detail::FindPassBindings<PassTag>(spaceId);
        if (srg == NullHandle)
        {
            return false;
        }
        SetShaderConstant(srg, input, value);
        return true;
    }

    //! Reap every per-pass ShaderBindings (created via CreatePassBindings)
    //! by its runtime PassShaderBindingsTag. Call once at pipeline / RenderSystem
    //! teardown — per-pass SRGs are persistent and have no external owner, so this is
    //! their single collection point.
    //!
    //! Destroyed here rather than tagged DeadTag like the view / instance SRGs: no tick
    //! follows teardown to reap DeadTag, so they would outlive RenderSystem, and their
    //! views hold images the render graph's pools own. Must run before
    //! RenderGraph::Shutdown releases those pools.
    inline void ReapPassShaderBindings(RHIContext& ctx)
    {
        eastl::vector<RHIHandle> dead;
        for (auto [entity, comp] : ctx.GetView<PassShaderBindingsTag, RHI::Components::ShaderBindings>().each())
        {
            dead.push_back(entity);
        }
        for (RHIHandle entity : dead)
        {
            ctx.DestoryEntity(entity);
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

    //! Whether a ReadPreviousImageAttachment slot has no previous frame to read (first
    //! frame, a descriptor change, a reader resuming). Its image is bound regardless, with
    //! undefined content — select it away rather than weighting it, as it may hold NaN.
    template<typename PassTag>
    bool IsPreviousFrameMissing(RHIContext& rhiCtx, RHI::InputName slot)
    {
        for (auto [handle, attachment] : rhiCtx.GetView<PassTag, ImagePassAttachment, PreviousFrameTag>().each())
        {
            if (attachment.m_slotName == slot)
            {
                return rhiCtx.Has<PreviousFrameMissingTag>(handle);
            }
        }

        LOG_ERROR("[IsPreviousFrameMissing] No previous-frame attachment in slot {}.", slot.GetCStr());
        return true;
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
