#pragma once

#include <EASTL/vector.h>

#include <RHI/ClearValue.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>

namespace Spark::RHI
{
    static constexpr uint32_t SwapChainRenderTargetIndex = ~0u;

    struct RenderTargetContextDescriptor
    {
        //! 资源的格式和数量配置，决定了 render target、depth stencil、resolve、shading rate 的格式。
        RenderAttachmentLayout m_attachmentLayout;

        //! 渲染目标的宽高。
        uint32_t m_imageWidth = 0;
        uint32_t m_imageHeight = 0;

        //! 多重缓冲数量（通常与 swap chain 的 image count 一致）。
        uint32_t m_bufferCount = 1;

        //! 指定哪个 render target 位置直接使用 swap chain 的 back buffer，而不是自己创建。
        //! 设为 SwapChainRenderTargetIndex 表示所有 render target 都由本模块创建。
        //! 例如设为 0，则第 0 个 render target 直接引用 swap chain image。
        uint32_t m_swapChainBackBufferIndex = SwapChainRenderTargetIndex;

        //! 各 attachment 的优化 clear value，按 attachmentLayout 中 m_attachmentFormats 的顺序对应。
        //! 可以为空，表示不指定优化 clear value。
        eastl::vector<ClearValue> m_optimizedClearValues;
    };
}
