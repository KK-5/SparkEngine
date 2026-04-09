#pragma once

#include <EASTL/vector.h>

#include <RHI/Base.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>

#include "RenderTargetContextDescriptor.h"

namespace Spark::RHI
{
    class Device;
    class ImagePool;

    //! 可选便利模块，管理一组按外部 frame index 访问的多重缓冲附件资源。
    //! 支持两类来源：
    //! 1) External: 外部提供每帧 Image，本模块创建并管理 ImageView。
    //! 2) Owned: 本模块按 ImageDescriptor 创建每帧 Image + ImageView。
    class RenderTargetContext
    {
    public:
        RenderTargetContext() = default;
        ~RenderTargetContext();

        ResultCode Init(Device& device, ImagePool& imagePool, const RenderTargetContextDescriptor& descriptor);

        void Shutdown();

        //! 使用新的 descriptor 重建资源
        ResultCode Resize(const RenderTargetContextDescriptor& descriptor);

        //////////////////////////////////////////////////////////////////////////
        // 按外部 frame index 访问

        //! 获取指定 frame index 的第 attachmentIndex 个 render target image / view。
        Image* GetRenderTarget(uint32_t frameIndex, uint32_t attachmentIndex) const;
        ImageView* GetRenderTargetView(uint32_t frameIndex, uint32_t attachmentIndex) const;

        //! 获取指定 frame index 的第 attachmentIndex 个 resolve target image / view。
        Image* GetResolveTarget(uint32_t frameIndex, uint32_t attachmentIndex) const;
        ImageView* GetResolveTargetView(uint32_t frameIndex, uint32_t attachmentIndex) const;

        //! 获取指定 frame index 的 depth stencil image / view。
        Image* GetDepthStencil(uint32_t frameIndex) const;
        ImageView* GetDepthStencilView(uint32_t frameIndex) const;

        //! 获取指定 frame index 的 shading rate image / view。
        Image* GetShadingRateImage(uint32_t frameIndex) const;
        ImageView* GetShadingRateImageView(uint32_t frameIndex) const;

        //////////////////////////////////////////////////////////////////////////
        // 查询

        //! 获取 render target 数量（不含 depth stencil、resolve、shading rate）
        uint32_t GetRenderTargetCount() const;

        //! 是否有 depth stencil attachment
        bool HasDepthStencil() const;

        //! 是否有 resolve target（即是否启用了 MSAA resolve）
        bool HasResolveTargets() const;

        //! 是否有 shading rate image
        bool HasShadingRateImage() const;

        //! 获取当前使用的 descriptor
        const RenderTargetContextDescriptor& GetDescriptor() const;

    private:
        bool ValidateDescriptor(const RenderTargetContextDescriptor& descriptor) const;
        void RebuildAttachmentIndices();
        ResultCode BuildResources();
        ResultCode BuildImageViews();
        void DestroyResources();
        static ImageBindFlags GetViewBindFlags(RTAttachmentUsage usage);
        static ImageAspectFlags GetViewAspectFlags(RTAttachmentUsage usage);
        uint32_t ResolveBufferIndex(uint32_t frameIndex) const;
        Image* GetImageByGlobalAttachmentIndex(uint32_t frameIndex, uint32_t globalAttachmentIndex) const;
        ImageView* GetImageViewByGlobalAttachmentIndex(uint32_t frameIndex, uint32_t globalAttachmentIndex) const;

        Device* m_device = nullptr;
        ImagePool* m_imagePool = nullptr;
        RenderTargetContextDescriptor m_descriptor;

        //! 按 attachment index 存储资源：m_images[attachmentIndex][bufferIndex]
        eastl::vector<eastl::vector<Ptr<Image>>> m_images;
        eastl::vector<eastl::vector<Ptr<ImageView>>> m_imageViews;

        //! 各类 attachment 在 m_descriptor.m_attachments 中的索引列表。
        eastl::vector<uint32_t> m_renderTargetIndices;
        eastl::vector<uint32_t> m_resolveIndices;
        uint32_t m_depthStencilIndex = Limits::Pipeline::RenderAttachmentCountMax;
        uint32_t m_shadingRateIndex = Limits::Pipeline::RenderAttachmentCountMax;
    };
}
