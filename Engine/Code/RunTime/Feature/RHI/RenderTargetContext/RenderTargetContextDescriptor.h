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
        External,

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

        //! 可选优化 clear 值
        bool m_hasOptimizedClearValue = false;
        ClearValue m_optimizedClearValue{};
    };

    struct RenderTargetContextDescriptor
    {
        uint32_t m_bufferCount = 1;
        eastl::vector<RTAttachmentDescriptor> m_attachments;
    };
}
