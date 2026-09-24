#pragma once

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Scissor/Scissor.h>
#include <RHI/Viewport/Viewport.h>

#include <Pass/Component/RHIComponents.h>
#include <View/ViewComponents.h>

//! Free-function utilities shared across the render graph (compiler + passes).
//! Keep these render-graph-scoped and dependency-light: they consume RHI-level
//! state but express render-layer decisions, so they live here rather than in RHI.
namespace Spark::Render
{
    //! Is a resource entity ready to be used by the render graph this frame?
    //!
    //! "Ready" requires BOTH:
    //!   1. Materialized — its owning RHI object exists (Components::Image::m_image or
    //!      Components::Buffer::m_buffer is non-null). Non-trivial resources are created
    //!      lazily by RHIResourceSystem (PendingImageInit / PendingBufferInit), so this
    //!      is false for a frame or two after the entity is declared.
    //!   2. Upload settled — no UploadPendingTag. A one-time staging upload is submitted
    //!      only by AsyncUploadSystem::SubmitBatch, which clears UploadPendingTag and
    //!      stamps PendingSync (the cross-queue fence the graph then waits on). Touching
    //!      the resource in the window between materialization and upload-submit would
    //!      advance its resource state (access) and make the still-pending EXCLUSIVE
    //!      upload fail AsyncUploadSystem's "already in use" check.
    //!
    //! Both the static-import barrier path (CompileStaticResourceBarriers) and
    //! imported-resource passes (e.g. SkyboxPass importing its sky cube) gate on this,
    //! so the two paths share a single definition of "ready".
    inline bool IsResourceReady(RHI::RHIContext& ctx, RHI::RHIHandle handle)
    {
        if (handle == RHI::NullHandle)
        {
            return false;
        }
        // One-time upload not yet submitted — defer. (Resources that never upload
        // never carry this tag, so this only gates the ones that do.)
        if (ctx.Has<RHI::UploadPendingTag>(handle))
        {
            return false;
        }
        if (const auto* img = ctx.TryGet<RHI::Components::Image>(handle))
        {
            return img->m_image != nullptr;
        }
        if (const auto* buf = ctx.TryGet<RHI::Components::Buffer>(handle))
        {
            return buf->m_buffer != nullptr;
        }
        return false;   // neither an image nor a buffer is materialized yet
    }

    //! Translate a pass attachment's usage/access into RHI AccessFlags. Buffer and
    //! image differ (a shader read on a buffer adds ConstantBufferRead; on an image
    //! it is SRV-only), so the two overloads pick the matching translation.
    inline RHI::AccessFlags ConvertAttachmentAccess(const BufferPassAttachment& att)
    {
        return RHI::ConvertBufferAccess(att.m_usage, att.m_access);
    }

    inline RHI::AccessFlags ConvertAttachmentAccess(const ImagePassAttachment& att)
    {
        return RHI::ConvertImageAccess(att.m_usage, att.m_access);
    }

    //! Compile a pass attachment into the resource state it implies. Fills access only;
    //! queue/stage stay at their defaults (Any / default) — callers that need the
    //! steady-state identity pin those themselves (see CompileStaticResourceBarriers).
    template <typename AttachmentT>
    RHI::ResourceState CompileResourceState(const AttachmentT& attachment)
    {
        ASSERT(attachment.m_usage != RHI::AttachmentUsage::Uninitialized,
            "[RenderGraphUtils] Attachment has uninitialized usage.");
        ASSERT(attachment.m_access != RHI::AttachmentAccess::Unknown,
            "[RenderGraphUtils] Attachment has unknown access.");

        RHI::ResourceState state;
        state.m_access = ConvertAttachmentAccess(attachment);
        return state;
    }

    //! The full-target viewport / scissor, which every view's rect is scaled against.
    //! RenderPassBeginInfo carries no render area and a depth-only pass has no color
    //! attachment, so the extent comes from the first attachment that exists.
    inline bool ResolveTargetViewport(
        const RHI::RenderPassBeginInfo& beginInfo, RHI::Viewport& viewport, RHI::Scissor& scissor)
    {
        const RHI::ImageView* target = beginInfo.m_colorAttachmentCount > 0
            ? beginInfo.m_colorAttachments[0].m_view
            : beginInfo.m_depthStencilAttachment.m_view;
        if (!target)
        {
            return false;
        }

        const RHI::Size extent = target->GetImage().GetDescriptor().m_size.GetReducedMip(
            target->GetDescriptor().m_mipSliceMin);

        viewport = RHI::Viewport(
            0.f, static_cast<float>(extent.m_width), 0.f, static_cast<float>(extent.m_height));
        scissor = RHI::Scissor(
            0, 0, static_cast<int32_t>(extent.m_width), static_cast<int32_t>(extent.m_height));
        return true;
    }

    //! A view's space1 SRG, if it declares one. The ViewShaderBindings component IS the
    //! declaration, which is what separates the two nulls:
    //!  - no component      -> the view binds no space1 at all (a pass whose shader has
    //!                         none still wants that view's viewport). Usable, out stays null.
    //!  - component, no SRG -> declared but not compiled yet, e.g. a view created this
    //!                         frame. NOT usable — drawing would leave space1 holding the
    //!                         previous pass's descriptors.
    inline bool ResolveViewShaderBindings(
        RHI::RHIContext& rhiContext, RHI::RHIHandle view, const RHI::ShaderBindings*& out)
    {
        out = nullptr;

        const auto* viewBindings = rhiContext.TryGet<ViewShaderBindings>(view);
        if (!viewBindings)
        {
            return true;
        }

        const auto* component =
            rhiContext.TryGet<RHI::Components::ShaderBindings>(viewBindings->m_bindings);
        if (!component || !component->m_bindings)
        {
            return false;
        }

        out = component->m_bindings.get();
        return true;
    }
}
