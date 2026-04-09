#pragma once

#include <EASTL/vector.h>

#include <RHI/ClearValue.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

namespace Spark::RHI
{
    static constexpr uint32_t SwapChainRenderTargetIndex = ~0u;
    
    enum class RTAttachmentUsage : uint8_t
    {
        RenderTarget,
        DepthStencil,
        Resolve,
        ShadingRate
    };

    enum class RTAttachmentSource : uint8_t
    {
        //! 外部提供每帧 Image（例如 swap chain back buffer），Context 仅创建 ImageView。
        External,

        //! Context 基于 ImageDescriptor 创建每帧 Image 和 ImageView。
        Owned
    };

    struct RTAttachmentDescriptor
    {
        RTAttachmentUsage m_usage = RTAttachmentUsage::RenderTarget;
        RTAttachmentSource m_source = RTAttachmentSource::Owned;

        //! source == External 时使用，大小必须等于 m_bufferCount。
        eastl::vector<Ptr<Image>> m_externalImages;

        //! source == Owned 时使用，作为每帧 image 创建描述。
        ImageDescriptor m_imageDescriptor;

        //! 可选优化 clear 值。
        bool m_hasOptimizedClearValue = false;
        ClearValue m_optimizedClearValue{};
    };

    struct RenderTargetContextDescriptor
    {
        //! 固定外部传入 frame index，这里仅声明缓冲数量。
        uint32_t m_bufferCount = 1;

        //! 显式声明需要管理的附件集合。
        eastl::vector<RTAttachmentDescriptor> m_attachments;
    };
}
