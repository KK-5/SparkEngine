#include "RenderTargetContext.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Device/DeviceLimits.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Resource/Image/ImagePool.h>

namespace Spark::RHI
{
    RenderTargetContext::~RenderTargetContext()
    {
        Shutdown();
    }

    ResultCode RenderTargetContext::Init(
        Device& device, ImagePool& imagePool, SwapChain& swapChain,
        const RenderTargetContextDescriptor& descriptor)
    {
        if (descriptor.m_imageWidth == 0 || descriptor.m_imageHeight == 0 || descriptor.m_bufferCount == 0)
        {
            LOG_WARN("[RenderTargetContext] Invalid descriptor: width, height and bufferCount must be non-zero.");
            return ResultCode::InvalidArgument;
        }

        m_device = &device;
        m_imagePool = &imagePool;
        m_swapChain = &swapChain;
        m_descriptor = descriptor;

        CollectAttachmentInfo();

        ResultCode result = CreateImages();
        if (result != ResultCode::Success)
        {
            return result;
        }

        return CreateImageViews();
    }

    void RenderTargetContext::Shutdown()
    {
        DestroyImages();
        m_attachmentInfos.clear();
        m_renderTargetIndices.clear();
        m_resolveIndices.clear();
        m_depthStencilIndex = InvalidRenderAttachmentIndex;
        m_shadingRateIndex = InvalidRenderAttachmentIndex;
        m_device = nullptr;
        m_imagePool = nullptr;
        m_swapChain = nullptr;
    }

    ResultCode RenderTargetContext::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            LOG_WARN("[RenderTargetContext] Resize dimensions must be non-zero.");
            return ResultCode::InvalidArgument;
        }

        DestroyImages();
        m_descriptor.m_imageWidth = width;
        m_descriptor.m_imageHeight = height;

        ResultCode result = CreateImages();
        if (result != ResultCode::Success)
        {
            return result;
        }

        return CreateImageViews();
    }

    void RenderTargetContext::CollectAttachmentInfo()
    {
        const RenderAttachmentLayout& layout = m_descriptor.m_attachmentLayout;
        const uint32_t attachmentCount = layout.m_attachmentCount;

        m_attachmentInfos.resize(attachmentCount);
        m_renderTargetIndices.clear();
        m_resolveIndices.clear();
        m_depthStencilIndex = InvalidRenderAttachmentIndex;
        m_shadingRateIndex = InvalidRenderAttachmentIndex;

        // 遍历所有 subpass，收集每个 attachment 的角色
        for (uint32_t sp = 0; sp < layout.m_subpassCount; ++sp)
        {
            const SubpassRenderAttachmentLayout& subpass = layout.m_subpassLayouts[sp];

            // Render targets
            for (uint32_t rt = 0; rt < subpass.m_rendertargetCount; ++rt)
            {
                const uint32_t idx = subpass.m_rendertargetDescriptors[rt].m_attachmentIndex;
                if (idx < attachmentCount && m_attachmentInfos[idx].m_role == AttachmentRole::None)
                {
                    m_attachmentInfos[idx].m_role = AttachmentRole::RenderTarget;
                    m_attachmentInfos[idx].m_bindFlags = ImageBindFlags::Color | ImageBindFlags::ShaderRead;
                    m_renderTargetIndices.push_back(idx);
                }

                // Resolve targets
                const uint32_t resolveIdx = subpass.m_rendertargetDescriptors[rt].m_resolveAttachmentIndex;
                if (resolveIdx != InvalidRenderAttachmentIndex &&
                    resolveIdx < attachmentCount && m_attachmentInfos[resolveIdx].m_role == AttachmentRole::None)
                {
                    m_attachmentInfos[resolveIdx].m_role = AttachmentRole::Resolve;
                    m_attachmentInfos[resolveIdx].m_bindFlags = ImageBindFlags::Color | ImageBindFlags::ShaderRead;
                    m_resolveIndices.push_back(resolveIdx);
                }
            }

            // Depth stencil
            const uint32_t dsIdx = subpass.m_depthStencilDescriptor.m_attachmentIndex;
            if (dsIdx != InvalidRenderAttachmentIndex &&
                dsIdx < attachmentCount && m_attachmentInfos[dsIdx].m_role == AttachmentRole::None)
            {
                m_attachmentInfos[dsIdx].m_role = AttachmentRole::DepthStencil;
                m_attachmentInfos[dsIdx].m_bindFlags = ImageBindFlags::DepthStencil | ImageBindFlags::ShaderRead;
                m_depthStencilIndex = dsIdx;
            }

            // Shading rate
            const uint32_t srIdx = subpass.m_shadingRateDescriptor.m_attachmentIndex;
            if (srIdx != InvalidRenderAttachmentIndex &&
                srIdx < attachmentCount && m_attachmentInfos[srIdx].m_role == AttachmentRole::None)
            {
                m_attachmentInfos[srIdx].m_role = AttachmentRole::ShadingRate;
                m_attachmentInfos[srIdx].m_bindFlags = ImageBindFlags::ShadingRate;
                m_shadingRateIndex = srIdx;
            }
        }
    }

    ResultCode RenderTargetContext::CreateImages()
    {
        const RenderAttachmentLayout& layout = m_descriptor.m_attachmentLayout;
        const uint32_t attachmentCount = layout.m_attachmentCount;
        const uint32_t bufferCount = m_descriptor.m_bufferCount;
        const uint32_t swapChainRT = m_descriptor.m_swapChainBackBufferIndex;

        m_images.resize(attachmentCount);

        for (uint32_t i = 0; i < attachmentCount; ++i)
        {
            const AttachmentInfo& info = m_attachmentInfos[i];
            if (info.m_role == AttachmentRole::None)
            {
                continue;
            }

            // swap chain back buffer 位置不创建资源
            if (i == swapChainRT)
            {
                continue;
            }

            // 确定该 attachment 需要几份
            // shading rate 只需一份，其余按 bufferCount
            const uint32_t imageCount = (info.m_role == AttachmentRole::ShadingRate) ? 1 : bufferCount;

            // 确定尺寸
            uint32_t width = m_descriptor.m_imageWidth;
            uint32_t height = m_descriptor.m_imageHeight;

            if (info.m_role == AttachmentRole::ShadingRate)
            {
                const Size& tileSize = m_device->GetLimits().m_shadingRateTileSize;
                width = (width + tileSize.m_width - 1) / tileSize.m_width;
                height = (height + tileSize.m_height - 1) / tileSize.m_height;
            }

            const Format format = layout.m_attachmentFormats[i];

            m_images[i].resize(imageCount);
            for (uint32_t buf = 0; buf < imageCount; ++buf)
            {
                Ptr<Image> image = Service<Factory>::Get()->CreateImage();

                ImageDescriptor imageDesc = ImageDescriptor::Create2D(
                    info.m_bindFlags, width, height, format);

                ImageInitRequest request;
                request.m_image = image.get();
                request.m_descriptor = imageDesc;
                request.m_optimizedClearValue =
                    (i < m_descriptor.m_optimizedClearValues.size())
                    ? &m_descriptor.m_optimizedClearValues[i] : nullptr;

                ResultCode result = m_imagePool->InitImage(request);
                if (result != ResultCode::Success)
                {
                    LOG_ERROR("[RenderTargetContext] Failed to create image [attachment={}, buffer={}].", i, buf);
                    DestroyImages();
                    return result;
                }

                m_images[i][buf] = eastl::move(image);
            }
        }

        return ResultCode::Success;
    }

    ResultCode RenderTargetContext::CreateImageViews()
    {
        const uint32_t attachmentCount = static_cast<uint32_t>(m_images.size());

        m_imageViews.resize(attachmentCount);

        for (uint32_t i = 0; i < attachmentCount; ++i)
        {
            const uint32_t imageCount = static_cast<uint32_t>(m_images[i].size());
            if (imageCount == 0)
            {
                continue;
            }

            const AttachmentInfo& info = m_attachmentInfos[i];

            // 构建 ImageViewDescriptor：mip 0 only，覆盖全部 array slice
            ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin = 0;
            viewDesc.m_mipSliceMax = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            viewDesc.m_overrideBindFlags = info.m_bindFlags;

            // depth stencil view 只访问 depth aspect
            if (info.m_role == AttachmentRole::DepthStencil)
            {
                viewDesc.m_aspectFlags = ImageAspectFlags::DepthStencil;
            }

            m_imageViews[i].resize(imageCount);
            for (uint32_t buf = 0; buf < imageCount; ++buf)
            {
                Ptr<ImageView> view = Service<Factory>::Get()->CreateImageView();

                ResultCode result = view->Init(*m_images[i][buf], viewDesc);
                if (result != ResultCode::Success)
                {
                    LOG_ERROR("[RenderTargetContext] Failed to create image view [attachment={}, buffer={}].", i, buf);
                    DestroyImages();
                    return result;
                }

                m_imageViews[i][buf] = eastl::move(view);
            }
        }

        return ResultCode::Success;
    }

    void RenderTargetContext::DestroyImages()
    {
        // ImageView 需要在 Image 之前释放
        m_imageViews.clear();
        m_images.clear();
    }

    uint32_t RenderTargetContext::GetCurrentBufferIndex() const
    {
        return m_swapChain->GetCurrentImageIndex() % m_descriptor.m_bufferCount;
    }

    //////////////////////////////////////////////////////////////////////////
    // 按当前帧索引访问

    Image* RenderTargetContext::GetCurrentRenderTarget(uint32_t index) const
    {
        return GetRenderTarget(GetCurrentBufferIndex(), index);
    }

    ImageView* RenderTargetContext::GetCurrentRenderTargetView(uint32_t index) const
    {
        return GetRenderTargetView(GetCurrentBufferIndex(), index);
    }

    Image* RenderTargetContext::GetCurrentResolveTarget(uint32_t index) const
    {
        return GetResolveTarget(GetCurrentBufferIndex(), index);
    }

    ImageView* RenderTargetContext::GetCurrentResolveTargetView(uint32_t index) const
    {
        return GetResolveTargetView(GetCurrentBufferIndex(), index);
    }

    Image* RenderTargetContext::GetCurrentDepthStencil() const
    {
        return GetDepthStencil(GetCurrentBufferIndex());
    }

    ImageView* RenderTargetContext::GetCurrentDepthStencilView() const
    {
        return GetDepthStencilView(GetCurrentBufferIndex());
    }

    //////////////////////////////////////////////////////////////////////////
    // 按显式帧索引访问 — Image

    Image* RenderTargetContext::GetRenderTarget(uint32_t bufferIndex, uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_renderTargetIndices.size())
        {
            return nullptr;
        }

        const uint32_t globalIndex = m_renderTargetIndices[attachmentIndex];

        // 该位置使用 swap chain 的 back buffer
        if (globalIndex == m_descriptor.m_swapChainBackBufferIndex)
        {
            return m_swapChain->GetImage(bufferIndex);
        }

        if (globalIndex < m_images.size() && bufferIndex < m_images[globalIndex].size())
        {
            return m_images[globalIndex][bufferIndex].get();
        }
        return nullptr;
    }

    Image* RenderTargetContext::GetResolveTarget(uint32_t bufferIndex, uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_resolveIndices.size())
        {
            return nullptr;
        }

        const uint32_t globalIndex = m_resolveIndices[attachmentIndex];
        if (globalIndex < m_images.size() && bufferIndex < m_images[globalIndex].size())
        {
            return m_images[globalIndex][bufferIndex].get();
        }
        return nullptr;
    }

    Image* RenderTargetContext::GetDepthStencil(uint32_t bufferIndex) const
    {
        if (m_depthStencilIndex != InvalidRenderAttachmentIndex &&
            m_depthStencilIndex < m_images.size() && bufferIndex < m_images[m_depthStencilIndex].size())
        {
            return m_images[m_depthStencilIndex][bufferIndex].get();
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // 按显式帧索引访问 — ImageView

    ImageView* RenderTargetContext::GetRenderTargetView(uint32_t bufferIndex, uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_renderTargetIndices.size())
        {
            return nullptr;
        }

        const uint32_t globalIndex = m_renderTargetIndices[attachmentIndex];

        // swap chain back buffer 的 view 不由本模块管理
        if (globalIndex == m_descriptor.m_swapChainBackBufferIndex)
        {
            return nullptr;
        }

        if (globalIndex < m_imageViews.size() && bufferIndex < m_imageViews[globalIndex].size())
        {
            return m_imageViews[globalIndex][bufferIndex].get();
        }
        return nullptr;
    }

    ImageView* RenderTargetContext::GetResolveTargetView(uint32_t bufferIndex, uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_resolveIndices.size())
        {
            return nullptr;
        }

        const uint32_t globalIndex = m_resolveIndices[attachmentIndex];
        if (globalIndex < m_imageViews.size() && bufferIndex < m_imageViews[globalIndex].size())
        {
            return m_imageViews[globalIndex][bufferIndex].get();
        }
        return nullptr;
    }

    ImageView* RenderTargetContext::GetDepthStencilView(uint32_t bufferIndex) const
    {
        if (m_depthStencilIndex != InvalidRenderAttachmentIndex &&
            m_depthStencilIndex < m_imageViews.size() && bufferIndex < m_imageViews[m_depthStencilIndex].size())
        {
            return m_imageViews[m_depthStencilIndex][bufferIndex].get();
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Shading rate

    Image* RenderTargetContext::GetShadingRateImage() const
    {
        if (m_shadingRateIndex != InvalidRenderAttachmentIndex &&
            m_shadingRateIndex < m_images.size() && !m_images[m_shadingRateIndex].empty())
        {
            return m_images[m_shadingRateIndex][0].get();
        }
        return nullptr;
    }

    ImageView* RenderTargetContext::GetShadingRateImageView() const
    {
        if (m_shadingRateIndex != InvalidRenderAttachmentIndex &&
            m_shadingRateIndex < m_imageViews.size() && !m_imageViews[m_shadingRateIndex].empty())
        {
            return m_imageViews[m_shadingRateIndex][0].get();
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // 查询

    uint32_t RenderTargetContext::GetRenderTargetCount() const
    {
        return static_cast<uint32_t>(m_renderTargetIndices.size());
    }

    bool RenderTargetContext::HasDepthStencil() const
    {
        return m_depthStencilIndex != InvalidRenderAttachmentIndex;
    }

    bool RenderTargetContext::HasResolveTargets() const
    {
        return !m_resolveIndices.empty();
    }

    bool RenderTargetContext::HasShadingRateImage() const
    {
        return m_shadingRateIndex != InvalidRenderAttachmentIndex;
    }

    const RenderTargetContextDescriptor& RenderTargetContext::GetDescriptor() const
    {
        return m_descriptor;
    }
}
