#pragma once

#include <EASTL/vector.h>

#include <RHI/Base.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>

#include "RenderTargetContextDescriptor.h"

namespace Spark::RHI
{
    class Device;
    class SwapChain;
    class ImagePool;

    //! 可选便利模块，管理一组随帧切换的渲染目标资源。
    //! 根据 RenderAttachmentLayout 自动创建 render target、depth stencil、resolve target 和 shading rate image。
    //! 通过 SwapChain 的 currentImageIndex 决定当前帧使用哪组多重缓冲资源。
    //! Resize 时自动销毁旧资源并按新尺寸重建。
    class RenderTargetContext
    {
    public:
        RenderTargetContext() = default;
        ~RenderTargetContext();

        ResultCode Init(Device& device, ImagePool& imagePool, SwapChain& swapChain,
                        const RenderTargetContextDescriptor& descriptor);

        void Shutdown();

        ResultCode Resize(uint32_t width, uint32_t height);

        //////////////////////////////////////////////////////////////////////////
        // 按当前帧索引访问（由 SwapChain::GetCurrentImageIndex 决定）

        //! 获取当前帧的第 index 个 render target image / view。
        Image* GetCurrentRenderTarget(uint32_t index) const;
        ImageView* GetCurrentRenderTargetView(uint32_t index) const;

        //! 获取当前帧的第 index 个 resolve target image / view（MSAA resolve）。
        //! 如果该 render target 不需要 resolve，返回 nullptr。
        Image* GetCurrentResolveTarget(uint32_t index) const;
        ImageView* GetCurrentResolveTargetView(uint32_t index) const;

        //! 获取当前帧的 depth stencil image / view。
        //! 如果没有 depth stencil attachment，返回 nullptr。
        Image* GetCurrentDepthStencil() const;
        ImageView* GetCurrentDepthStencilView() const;

        //////////////////////////////////////////////////////////////////////////
        // 按显式帧索引访问

        //! 获取指定帧索引的第 attachmentIndex 个 render target image / view。
        Image* GetRenderTarget(uint32_t bufferIndex, uint32_t attachmentIndex) const;
        ImageView* GetRenderTargetView(uint32_t bufferIndex, uint32_t attachmentIndex) const;

        //! 获取指定帧索引的第 attachmentIndex 个 resolve target image / view。
        Image* GetResolveTarget(uint32_t bufferIndex, uint32_t attachmentIndex) const;
        ImageView* GetResolveTargetView(uint32_t bufferIndex, uint32_t attachmentIndex) const;

        //! 获取指定帧索引的 depth stencil image / view。
        Image* GetDepthStencil(uint32_t bufferIndex) const;
        ImageView* GetDepthStencilView(uint32_t bufferIndex) const;

        //////////////////////////////////////////////////////////////////////////
        // Shading rate image（不分帧，单份）

        //! 获取 shading rate image / view。如果未启用 VRS，返回 nullptr。
        Image* GetShadingRateImage() const;
        ImageView* GetShadingRateImageView() const;

        //////////////////////////////////////////////////////////////////////////
        // 查询

        //! 获取 render target 数量（不含 depth stencil、resolve、shading rate）。
        uint32_t GetRenderTargetCount() const;

        //! 是否有 depth stencil attachment。
        bool HasDepthStencil() const;

        //! 是否有 resolve target（即是否启用了 MSAA resolve）。
        bool HasResolveTargets() const;

        //! 是否有 shading rate image。
        bool HasShadingRateImage() const;

        //! 获取当前使用的 descriptor。
        const RenderTargetContextDescriptor& GetDescriptor() const;

    private:
        //! 从 RenderAttachmentLayout 的所有 subpass 中收集每个 attachment 的角色。
        enum class AttachmentRole : uint8_t
        {
            None = 0,
            RenderTarget,
            Resolve,
            DepthStencil,
            ShadingRate
        };

        struct AttachmentInfo
        {
            AttachmentRole m_role = AttachmentRole::None;
            ImageBindFlags m_bindFlags = ImageBindFlags::None;
        };

        void CollectAttachmentInfo();
        ResultCode CreateImages();
        ResultCode CreateImageViews();
        void DestroyImages();

        uint32_t GetCurrentBufferIndex() const;

        Device* m_device = nullptr;
        ImagePool* m_imagePool = nullptr;
        SwapChain* m_swapChain = nullptr;
        RenderTargetContextDescriptor m_descriptor;

        //! 每个 attachment index 对应的角色和 bind flags，大小为 m_attachmentCount。
        eastl::vector<AttachmentInfo> m_attachmentInfos;

        //! 按 attachment index 存储的所有 image 和 image view 资源。
        //! m_images[attachmentIndex][bufferIndex]，不需要多重缓冲的只有一份。
        //! swap chain back buffer 位置和未使用的位置为空 vector。
        eastl::vector<eastl::vector<Ptr<Image>>> m_images;
        eastl::vector<eastl::vector<Ptr<ImageView>>> m_imageViews;

        //! 各类 attachment 在 m_attachmentFormats 中的 index 列表，用于快速查找。
        eastl::vector<uint32_t> m_renderTargetIndices;
        eastl::vector<uint32_t> m_resolveIndices;
        uint32_t m_depthStencilIndex = InvalidRenderAttachmentIndex;
        uint32_t m_shadingRateIndex = InvalidRenderAttachmentIndex;
    };
}
