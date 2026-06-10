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

    //! Returned by CreatePassShaderBindings. Bundles the ShaderBindings Ptr
    //! (for SetXxx data calls) with the RHIContext entity (for dirty marking
    //! via MarkShaderBindingsUpdate and for lifetime management).
    //! m_bindings == nullptr / m_entity == NullHandle indicates creation failed.
    struct PassShaderBindingsHandle
    {
        Ptr<RHI::ShaderBindings> m_bindings;
        RHIHandle                m_entity = NullHandle;
    };

    //! Allocate a ShaderBindings against the named pass's input layout and
    //! register it on the RHIContext so RenderGraphCompiler::CompileShaderInputs
    //! can find it. The returned handle is initialized with ShaderBindingsUpdateTag
    //! so the very first compile sweep picks it up and produces valid GPU bindings
    //! before the first execute.
    //!
    //! Intended for once-per-feature-init use. Callers store the returned
    //! struct, call SetXxx on m_bindings to update data, then call
    //! MarkShaderBindingsUpdate(rhiCtx, .m_entity) to schedule a recompile.
    //!
    //! Returns a default-constructed handle (m_bindings == nullptr) if the
    //! pass has no PassPipelineLayout (e.g. custom-pipeline pass), or if RHI
    //! services / Init fail.
    template<typename PassTagT>
    PassShaderBindingsHandle CreatePassShaderBindings(
        PassContext& passCtx, RHIContext& rhiCtx, uint32_t spaceId)
    {
        Pass pass = FindPass<PassTagT>(passCtx);

        if (!passCtx.Has<PassPipelineLayout>(pass))
        {
            LOG_ERROR("[CreatePassShaderBindings] Pass has no PassPipelineLayout "
                      "(custom-pipeline pass or shader reflection unavailable).");
            return {};
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
            return {};
        }

        // Register on RHIContext for CompileShaderInputs discovery; flag as
        // dirty so the very first frame compiles the bindings before execute.
        RHIHandle entity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(entity, RHI::Components::ShaderBindings{ bindings });
        rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(entity);

        return PassShaderBindingsHandle{ bindings, entity };
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

    //! Attach ShaderBindings to a pass so the executer auto-binds it at pass begin.
    //! Semantics:
    //!   - One entry per (pass, spaceId). Re-attaching the same spaceId overwrites
    //!     the previous Ptr — last attach wins, no per-frame clearing.
    //!   - Detach = pass nullptr; the spaceId entry is kept (preserves attach
    //!     order for the remaining slots) but skipped at bind time.
    //!   - Bind order is attach order. Caller controls ordering if it matters.
    template<typename PassTagT>
    void AttachShaderBindings(
        PassContext& passCtx, uint32_t spaceId, Ptr<RHI::ShaderBindings> bindings)
    {
        Pass pass = FindPass<PassTagT>(passCtx);

        auto& comp = passCtx.Has<PassShaderBindings>(pass)
            ? passCtx.Get<PassShaderBindings>(pass)
            : passCtx.Add<PassShaderBindings>(pass, PassShaderBindings{});

        for (auto& e : comp.m_entries)
        {
            if (e.m_spaceId == spaceId)
            {
                e.m_bindings = eastl::move(bindings);
                return;
            }
        }
        ASSERT(comp.m_entries.size() < comp.m_entries.max_size(),
            "AttachShaderBindings: exceeded ShaderInputGroupCountMax ({}).",
            static_cast<uint32_t>(comp.m_entries.max_size()));
        comp.m_entries.push_back(PassShaderBindings::Entry{ spaceId, eastl::move(bindings) });
    }


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

            RHIHandle viewEntity = attachment.m_view;
            auto* viewHier = rhiCtx.TryGet<RHI::ViewHierarchy>(viewEntity);
            if (!viewHier)
            {
                LOG_ERROR("[FindPassAttachmentImage] View entity has no ViewHierarchy component (slot: {}).",
                    slot.GetCStr());
                break;
            }
            RHIHandle image = viewHier->m_resource;
            auto* backImage = rhiCtx.TryGet<BackingImage>(image);
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

            RHIHandle viewEntity = attachment.m_view;
            auto* viewHier = rhiCtx.TryGet<RHI::ViewHierarchy>(viewEntity);
            if (!viewHier)
            {
                LOG_ERROR("[FindPassAttachmentBuffer] View entity has no ViewHierarchy component (slot: {}).",
                    slot.GetCStr());
                break;
            }
            RHIHandle buffer = viewHier->m_resource;
            auto* backBuffer = rhiCtx.TryGet<BackingBuffer>(buffer);
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
