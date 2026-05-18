/*
 * SparkEngine RHI - dynamic render pass begin info.
 *
 * Carries everything vkCmdBeginRendering (VkRenderingInfo) and
 * ID3D12GraphicsCommandList4::BeginRenderPass need to open a render pass
 * without exposing any VkRenderPass / VkFramebuffer object to the RHI caller.
 */
#pragma once

#include <EASTL/array.h>

#include <RHI/RHILimits.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>

namespace Spark::RHI
{
    class ImageView;

    /// One color render target attachment inside a RenderPassBeginInfo.
    struct RenderPassColorAttachment
    {
        /// Color render target view. Required.
        const ImageView* m_view = nullptr;

        /// Optional MSAA resolve target. Backend resolves into this view at EndRenderPass.
        /// nullptr disables resolve for this attachment.
        const ImageView* m_resolveView = nullptr;

        /// Load / store action and clear value.
        AttachmentLoadStoreAction m_loadStoreAction;
    };

    /// Optional depth-stencil attachment inside a RenderPassBeginInfo.
    struct RenderPassDepthStencilAttachment
    {
        /// Depth-stencil view. nullptr disables the depth-stencil attachment.
        const ImageView* m_view = nullptr;

        /// Read-only DSV when Read is set and Write is not; otherwise writable DSV.
        AttachmentAccess m_access = AttachmentAccess::ReadWrite;

        /// Load / store action (depth and stencil variants live in AttachmentLoadStoreAction).
        AttachmentLoadStoreAction m_loadStoreAction;
    };

    /// Parameters for CommandList::BeginRenderPass.
    ///
    /// Designed for dynamic rendering only (Vulkan 1.3 / VK_KHR_dynamic_rendering,
    /// DX12 ID3D12GraphicsCommandList4::BeginRenderPass). No VkRenderPass / VkFramebuffer
    /// object is implied or required.
    struct RenderPassBeginInfo
    {
        /// Number of valid entries in m_colorAttachments.
        uint32_t m_colorAttachmentCount = 0;

        /// Color render target attachments.
        eastl::array<RenderPassColorAttachment, Limits::Pipeline::AttachmentColorCountMax> m_colorAttachments;

        /// Optional depth-stencil attachment. Disabled when its m_view is nullptr.
        RenderPassDepthStencilAttachment m_depthStencilAttachment;

        /// Optional per-region variable-rate shading rate image. nullptr disables VRS per-region.
        const ImageView* m_shadingRateAttachment = nullptr;

        /// Layers rendered when using a layered/array attachment. Used when m_viewMask is 0.
        uint32_t m_layerCount = 1;

        /// Vulkan multiview mask. 0 disables multiview (default). Ignored by DX12.
        uint32_t m_viewMask = 0;
    };
}
