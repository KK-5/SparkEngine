/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Removed streaming concepts (StreamingImagePool, m_streamedMipLevel, etc.);
 *    streaming is the upload manager's responsibility. Image is a pure resource
 *    description holding memory.
 * Modified by SparkEngine in 2026
 *  -- Removed subresource state tracking (D3D12_RESOURCE_STATES): RHI::Image records it.
 *  -- Subresource layouts moved to RHI::Image, which computes them from DeviceLimits.
 */

#pragma once

#include <EASTL/vector.h>
#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Resource/Image/Image.h>
#include <DX12.h>
#include <MemoryView.h>

namespace Spark::RHI::DX12
{
    /**
     * Immutable physical tile layout for a tiled (reserved) image, computed at creation
     * from GetResourceTiling. For committed resources all fields are zero.
     */
    struct ImageTileLayout
    {
        bool IsPacked(uint32_t subresourceIndex) const;

        uint32_t GetPackedSubresourceIndex() const;

        uint32_t GetTileOffset(uint32_t subresourceIndex) const;

        void GetSubresourceTileInfo(
            uint32_t subresourceIndex,
            uint32_t& imageTileOffset,
            D3D12_TILED_RESOURCE_COORDINATE& coordinate,
            D3D12_TILE_REGION_SIZE& regionSize) const;

        RHI::Size m_tileSize;
        uint32_t m_tileCount = 0;
        uint32_t m_tileCountStandard = 0;
        uint32_t m_tileCountPacked = 0;
        uint32_t m_mipCount = 0;
        uint32_t m_mipCountStandard = 0;
        uint32_t m_mipCountPacked = 0;
        eastl::vector<D3D12_SUBRESOURCE_TILING> m_subresourceTiling;
    };

    class Image final : public RHI::Image
    {
        using Base = RHI::Image;
    public:
        ~Image() = default;

        const MemoryView& GetMemoryView() const;
        MemoryView& GetMemoryView();

        bool IsTiled() const;

    private:
        Image() = default;

        friend class SwapChain;
        friend class ImagePool;
        friend class TransientResourcePool;
        friend class DeviceObjectFactory<Image>;

        MemoryView m_memoryView;

        size_t m_sizeInBytes = 0;

        ImageTileLayout m_tileLayout;
    };
}