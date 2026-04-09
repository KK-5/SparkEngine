#include "RenderTargetContext.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Image/ImagePool.h>

namespace Spark::RHI
{
    RenderTargetContext::~RenderTargetContext()
    {
        Shutdown();
    }

    ResultCode RenderTargetContext::Init(
        Device& device, ImagePool& imagePool,
        const RenderTargetContextDescriptor& descriptor)
    {
        if (!ValidateDescriptor(descriptor))
        {
            return ResultCode::InvalidArgument;
        }

        Shutdown();

        m_device = &device;
        m_imagePool = &imagePool;
        m_descriptor = descriptor;

        RebuildAttachmentIndices();

        ResultCode result = BuildResources();
        if (result != ResultCode::Success)
        {
            return result;
        }

        return BuildImageViews();
    }

    void RenderTargetContext::Shutdown()
    {
        DestroyResources();
        m_renderTargetIndices.clear();
        m_resolveIndices.clear();
        m_depthStencilIndex = Limits::Pipeline::RenderAttachmentCountMax;
        m_shadingRateIndex = Limits::Pipeline::RenderAttachmentCountMax;
        m_descriptor = {};
        m_device = nullptr;
        m_imagePool = nullptr;
    }

    void RenderTargetContext::Begin()
    {
    }

    void RenderTargetContext::End()
    {
        m_currentFrameIndex = (m_currentFrameIndex + 1) % m_descriptor.m_bufferCount;
    }

    ResultCode RenderTargetContext::Resize(const RenderTargetContextDescriptor& descriptor)
    {
        if (!m_device || !m_imagePool)
        {
            LOG_WARN("[RenderTargetContext] Resize called before Init.");
            return ResultCode::InvalidArgument;
        }

        if (!ValidateDescriptor(descriptor))
        {
            return ResultCode::InvalidArgument;
        }

        DestroyResources();
        m_descriptor = descriptor;
        m_currentFrameIndex = 0;
        RebuildAttachmentIndices();

        ResultCode result = BuildResources();
        if (result != ResultCode::Success)
        {
            return result;
        }

        return BuildImageViews();
    }

    bool RenderTargetContext::ValidateDescriptor(const RenderTargetContextDescriptor& descriptor) const
    {
        if (descriptor.m_bufferCount == 0)
        {
            LOG_WARN("[RenderTargetContext] Invalid descriptor: m_bufferCount must be non-zero.");
            return false;
        }

        uint32_t depthStencilCount = 0;
        uint32_t shadingRateCount = 0;

        for (uint32_t i = 0; i < descriptor.m_attachments.size(); ++i)
        {
            const RTAttachmentDescriptor& attachmentDesc = descriptor.m_attachments[i];
            if (attachmentDesc.m_usage == RTAttachmentUsage::DepthStencil)
            {
                ++depthStencilCount;
            }
            else if (attachmentDesc.m_usage == RTAttachmentUsage::ShadingRate)
            {
                ++shadingRateCount;
            }

            if (attachmentDesc.m_source == RTAttachmentSource::External)
            {
                if (attachmentDesc.m_externalImages.size() != descriptor.m_bufferCount)
                {
                    LOG_WARN(
                        "[RenderTargetContext] External attachment[{}] image count mismatch. expected={}, actual={}.",
                        i, descriptor.m_bufferCount, attachmentDesc.m_externalImages.size());
                    return false;
                }

                for (uint32_t bufferIndex = 0; bufferIndex < attachmentDesc.m_externalImages.size(); ++bufferIndex)
                {
                    if (!attachmentDesc.m_externalImages[bufferIndex])
                    {
                        LOG_WARN(
                            "[RenderTargetContext] External attachment[{}] has null image at bufferIndex={}.",
                            i, bufferIndex);
                        return false;
                    }
                }
            }
            else if (attachmentDesc.m_imageDescriptor.m_format == Format::Unknown)
            {
                LOG_WARN("[RenderTargetContext] Owned attachment[{}] must provide a valid image format.", i);
                return false;
            }
        }

        if (depthStencilCount > 1)
        {
            LOG_WARN("[RenderTargetContext] Only one depth-stencil attachment is supported.");
            return false;
        }
        if (shadingRateCount > 1)
        {
            LOG_WARN("[RenderTargetContext] Only one shading-rate attachment is supported.");
            return false;
        }

        return true;
    }

    void RenderTargetContext::RebuildAttachmentIndices()
    {
        m_renderTargetIndices.clear();
        m_resolveIndices.clear();
        m_depthStencilIndex = Limits::Pipeline::RenderAttachmentCountMax;
        m_shadingRateIndex = Limits::Pipeline::RenderAttachmentCountMax;

        for (uint32_t i = 0; i < m_descriptor.m_attachments.size(); ++i)
        {
            switch (m_descriptor.m_attachments[i].m_usage)
            {
            case RTAttachmentUsage::RenderTarget:
                m_renderTargetIndices.push_back(i);
                break;
            case RTAttachmentUsage::Resolve:
                m_resolveIndices.push_back(i);
                break;
            case RTAttachmentUsage::DepthStencil:
                m_depthStencilIndex = i;
                break;
            case RTAttachmentUsage::ShadingRate:
                m_shadingRateIndex = i;
                break;
            default:
                break;
            }
        }
    }

    ResultCode RenderTargetContext::BuildResources()
    {
        const uint32_t attachmentCount = static_cast<uint32_t>(m_descriptor.m_attachments.size());
        const uint32_t bufferCount = m_descriptor.m_bufferCount;

        m_images.clear();
        m_images.resize(attachmentCount);

        for (uint32_t attachmentIndex = 0; attachmentIndex < attachmentCount; ++attachmentIndex)
        {
            const RTAttachmentDescriptor& attachmentDesc = m_descriptor.m_attachments[attachmentIndex];
            m_images[attachmentIndex].resize(bufferCount);

            if (attachmentDesc.m_source == RTAttachmentSource::External)
            {
                for (uint32_t bufferIndex = 0; bufferIndex < bufferCount; ++bufferIndex)
                {
                    m_images[attachmentIndex][bufferIndex] = attachmentDesc.m_externalImages[bufferIndex];
                }
                continue;
            }

            for (uint32_t bufferIndex = 0; bufferIndex < bufferCount; ++bufferIndex)
            {
                Ptr<Image> image = Service<Factory>::Get()->CreateImage();
                if (!image)
                {
                    LOG_ERROR(
                        "[RenderTargetContext] Failed to allocate image object [attachment={}, buffer={}].",
                        attachmentIndex, bufferIndex);
                    DestroyResources();
                    return ResultCode::Fail;
                }

                ImageInitRequest request;
                request.m_image = image.get();
                request.m_descriptor = attachmentDesc.m_imageDescriptor;
                request.m_optimizedClearValue = attachmentDesc.m_hasOptimizedClearValue
                    ? &attachmentDesc.m_optimizedClearValue
                    : nullptr;

                const ResultCode result = m_imagePool->InitImage(request);
                if (result != ResultCode::Success)
                {
                    LOG_ERROR(
                        "[RenderTargetContext] Failed to create owned image [attachment={}, buffer={}].",
                        attachmentIndex, bufferIndex);
                    DestroyResources();
                    return result;
                }

                m_images[attachmentIndex][bufferIndex] = eastl::move(image);
            }
        }

        return ResultCode::Success;
    }

    ResultCode RenderTargetContext::BuildImageViews()
    {
        const uint32_t attachmentCount = static_cast<uint32_t>(m_images.size());
        m_imageViews.clear();
        m_imageViews.resize(attachmentCount);

        for (uint32_t attachmentIndex = 0; attachmentIndex < attachmentCount; ++attachmentIndex)
        {
            const RTAttachmentDescriptor& attachmentDesc = m_descriptor.m_attachments[attachmentIndex];
            const uint32_t bufferCount = static_cast<uint32_t>(m_images[attachmentIndex].size());

            ImageViewDescriptor viewDesc;
            viewDesc.m_overrideBindFlags = GetViewBindFlags(attachmentDesc.m_usage);
            viewDesc.m_aspectFlags = GetViewAspectFlags(attachmentDesc.m_usage);

            m_imageViews[attachmentIndex].resize(bufferCount);
            for (uint32_t bufferIndex = 0; bufferIndex < bufferCount; ++bufferIndex)
            {
                if (!m_images[attachmentIndex][bufferIndex])
                {
                    LOG_ERROR(
                        "[RenderTargetContext] Attachment image is null [attachment={}, buffer={}].",
                        attachmentIndex, bufferIndex);
                    DestroyResources();
                    return ResultCode::InvalidArgument;
                }

                Ptr<ImageView> view = Service<Factory>::Get()->CreateImageView();
                if (!view)
                {
                    LOG_ERROR(
                        "[RenderTargetContext] Failed to allocate image view object [attachment={}, buffer={}].",
                        attachmentIndex, bufferIndex);
                    DestroyResources();
                    return ResultCode::Fail;
                }

                const ResultCode result = view->Init(*m_images[attachmentIndex][bufferIndex], viewDesc);
                if (result != ResultCode::Success)
                {
                    LOG_ERROR(
                        "[RenderTargetContext] Failed to create image view [attachment={}, buffer={}].",
                        attachmentIndex, bufferIndex);
                    DestroyResources();
                    return result;
                }

                m_imageViews[attachmentIndex][bufferIndex] = eastl::move(view);
            }
        }

        return ResultCode::Success;
    }

    void RenderTargetContext::DestroyResources()
    {
        m_imageViews.clear();
        m_images.clear();
    }

    ImageBindFlags RenderTargetContext::GetViewBindFlags(RTAttachmentUsage usage)
    {
        switch (usage)
        {
        case RTAttachmentUsage::RenderTarget:
            return ImageBindFlags::Color;
        case RTAttachmentUsage::DepthStencil:
            return ImageBindFlags::DepthStencil;
        case RTAttachmentUsage::Resolve:
            return ImageBindFlags::Color;
        case RTAttachmentUsage::ShadingRate:
            return ImageBindFlags::ShadingRate;
        default:
            return ImageBindFlags::None;
        }
    }

    ImageAspectFlags RenderTargetContext::GetViewAspectFlags(RTAttachmentUsage usage)
    {
        if (usage == RTAttachmentUsage::DepthStencil)
        {
            return ImageAspectFlags::DepthStencil;
        }
        return ImageAspectFlags::All;
    }

    uint32_t RenderTargetContext::ResolveBufferIndex(uint32_t frameIndex) const
    {
        if (m_descriptor.m_bufferCount == 0)
        {
            return 0;
        }
        return frameIndex % m_descriptor.m_bufferCount;
    }

    Image* RenderTargetContext::GetImageByGlobalAttachmentIndex(uint32_t frameIndex, uint32_t globalAttachmentIndex) const
    {
        if (globalAttachmentIndex >= m_images.size())
        {
            return nullptr;
        }

        const uint32_t bufferIndex = ResolveBufferIndex(frameIndex);
        if (bufferIndex >= m_images[globalAttachmentIndex].size())
        {
            return nullptr;
        }
        return m_images[globalAttachmentIndex][bufferIndex].get();
    }

    ImageView* RenderTargetContext::GetImageViewByGlobalAttachmentIndex(uint32_t frameIndex, uint32_t globalAttachmentIndex) const
    {
        if (globalAttachmentIndex >= m_imageViews.size())
        {
            return nullptr;
        }

        const uint32_t bufferIndex = ResolveBufferIndex(frameIndex);
        if (bufferIndex >= m_imageViews[globalAttachmentIndex].size())
        {
            return nullptr;
        }
        return m_imageViews[globalAttachmentIndex][bufferIndex].get();
    }

    Image* RenderTargetContext::GetRenderTarget(uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_renderTargetIndices.size())
        {
            return nullptr;
        }
        return GetImageByGlobalAttachmentIndex(m_currentFrameIndex, m_renderTargetIndices[attachmentIndex]);
    }

    ImageView* RenderTargetContext::GetRenderTargetView(uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_renderTargetIndices.size())
        {
            return nullptr;
        }
        return GetImageViewByGlobalAttachmentIndex(m_currentFrameIndex, m_renderTargetIndices[attachmentIndex]);
    }

    Image* RenderTargetContext::GetResolveTarget(uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_resolveIndices.size())
        {
            return nullptr;
        }
        return GetImageByGlobalAttachmentIndex(m_currentFrameIndex, m_resolveIndices[attachmentIndex]);
    }

    ImageView* RenderTargetContext::GetResolveTargetView(uint32_t attachmentIndex) const
    {
        if (attachmentIndex >= m_resolveIndices.size())
        {
            return nullptr;
        }
        return GetImageViewByGlobalAttachmentIndex(m_currentFrameIndex, m_resolveIndices[attachmentIndex]);
    }

    Image* RenderTargetContext::GetDepthStencil() const
    {
        return GetImageByGlobalAttachmentIndex(m_currentFrameIndex, m_depthStencilIndex);
    }

    ImageView* RenderTargetContext::GetDepthStencilView() const
    {
        return GetImageViewByGlobalAttachmentIndex(m_currentFrameIndex, m_depthStencilIndex);
    }

    Image* RenderTargetContext::GetShadingRateImage() const
    {
        return GetImageByGlobalAttachmentIndex(m_currentFrameIndex, m_shadingRateIndex);
    }

    ImageView* RenderTargetContext::GetShadingRateImageView() const
    {
        return GetImageViewByGlobalAttachmentIndex(m_currentFrameIndex, m_shadingRateIndex);
    }

    uint32_t RenderTargetContext::GetRenderTargetCount() const
    {
        return static_cast<uint32_t>(m_renderTargetIndices.size());
    }

    bool RenderTargetContext::HasDepthStencil() const
    {
        return m_depthStencilIndex != Limits::Pipeline::RenderAttachmentCountMax;
    }

    bool RenderTargetContext::HasResolveTargets() const
    {
        return !m_resolveIndices.empty();
    }

    bool RenderTargetContext::HasShadingRateImage() const
    {
        return m_shadingRateIndex != Limits::Pipeline::RenderAttachmentCountMax;
    }

    const RenderTargetContextDescriptor& RenderTargetContext::GetDescriptor() const
    {
        return m_descriptor;
    }
}
