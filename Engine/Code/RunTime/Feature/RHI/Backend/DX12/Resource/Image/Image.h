/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- All subresource states are stored in vector<D3D12_RESOURCE_STATES>
 *  -- Removed streaming concepts (StreamingImagePool, m_streamedMipLevel, etc.);
 *    streaming is the upload manager's responsibility. Image is a pure resource
 *    description holding memory, layout metadata, and subresource state tracking.
 */

#pragma once

#include <EASTL/vector.h>
#include <EASTL/array.h>
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

        struct SubresourceRangeState
        {
            RHI::ImageSubresourceRange m_range;
            D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
        };

        void SetSubresourceState(D3D12_RESOURCE_STATES state, const RHI::ImageSubresourceRange* range = nullptr);

        void SetSubresourceState(D3D12_RESOURCE_STATES state, uint32_t subresourceIndex);

        eastl::vector<SubresourceRangeState> GetSubresourceStateByRange(const RHI::ImageSubresourceRange* range = nullptr) const;

        D3D12_RESOURCE_STATES GetInitialResourceState() const;

    private:
        Image() = default;

        friend class SwapChain;
        friend class ImagePool;
        friend class TransientResourcePool;
        friend class DeviceObjectFactory<Image>;

        //////////////////////////////////////////////////////////////////////////
        // RHI::Image
        void GetSubresourceLayoutsInternal(
            const RHI::ImageSubresourceRange& subresourceRange,
            RHI::ImageSubresourceLayout* subresourceLayouts,
            size_t* totalSizeInBytes) const override;
        //////////////////////////////////////////////////////////////////////////

        void GetSubresourceIndexByRange(const RHI::ImageSubresourceRange* range, uint32_t& indexStart, uint32_t& indexEnd) const;

        void GenerateSubresourceLayouts();

        void InitSubresourceState();

        MemoryView m_memoryView;

        size_t m_sizeInBytes = 0;

        eastl::array<RHI::ImageSubresourceLayout, RHI::Limits::Image::MipCountMax> m_subresourceLayoutsPerMipChain;

        ImageTileLayout m_tileLayout;

        eastl::vector<D3D12_RESOURCE_STATES> m_subresourceState;

        D3D12_RESOURCE_STATES m_initialResourceState = D3D12_RESOURCE_STATE_COMMON;
    };
}