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

    //! Allocate a ShaderBindings against the named pass's input layout and
    //! register it on the RHIContext so RenderGraphCompiler::CompileShaderInputs
    //! can find it. The entity is created with ShaderBindingsUpdateTag so the very
    //! first compile sweep picks it up and produces valid GPU bindings before the
    //! first execute.
    //!
    //! Returns the binding ENTITY (RHIHandle) only — not the RHI::ShaderBindings
    //! Ptr. Stage data through the entity with the Render::SetShaderXxx helpers
    //! (Shader/ShaderBindingsUtils.h), which also mark it dirty; push the entity
    //! into a DrawRequest's m_shaderBindingEntities so the draw references it. The
    //! entity owns the binding's lifetime (Components::ShaderBindings holds the Ptr),
    //! so destroying the entity releases it.
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

        if (!result)
        {
            LOG_ERROR("[FindPassAttachmentImage] No attachment image found for slot '{}'.", slot.GetCStr());
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

        if (!result)
        {
            LOG_ERROR("[FindPassAttachmentImageView] No attachment image view found for slot '{}'.", slot.GetCStr());
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

        if (!result)
        {
            LOG_ERROR("[FindPassAttachmentBuffer] No attachment buffer found for slot '{}'.", slot.GetCStr());
        }
        return result;
    }
}
