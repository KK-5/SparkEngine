/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/vector.h>
#include <RHI/Resource/Image/Image.h>
#include <DX12.h>
#include <MemoryView.h>

namespace Spark::RHI::DX12
{
    /**
     * Contains the tiled resource layout for an image. More than one sub-resources can be packed
     * into one or more tiles. The lowest N mips are typically packed into one or two tiles. The rest
     * of the mips are considered 'standard' and are composed of one or more tiles.
     */
    struct ImageTileLayout
    {
        // Returns whether the subresource is packed into a tile with other subresources.
        bool IsPacked(uint32_t subresourceIndex) const;

        // Returns the first subresource index associated with packed mips.
        uint32_t GetPackedSubresourceIndex() const;

        // Returns the tile offset relative to the image.
        uint32_t GetTileOffset(uint32_t subresourceIndex) const;

        /**
         * Given a subresource index, returns the tile offset of the subresource from the total
         * image tile set. The coordinate and region size are used to describe how the tiles map
         * to the source image. Packed mips are treated as a simple region of flat tiles.
         */
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

        // Returns the memory view allocated to this buffer.
        const MemoryView& GetMemoryView() const;
        MemoryView& GetMemoryView();

        // Get mip level uploaded to GPU
        uint32_t GetStreamedMipLevel() const;

        void SetStreamedMipLevel(uint32_t streamedMipLevel);

        // Returns whether the image is using a tiled resource.
        bool IsTiled() const;
        
        // Describes the state of a subresource by index.
        struct SubresourceAttachmentState
        {
            uint32_t m_subresourceIndex = 0;
            D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
        };

        // Describes the resource state of a range of subresources.
        using SubresourceRangeAttachmentState = RHI::ImageProperty<D3D12_RESOURCE_STATES>::PropertyRange;

    };
}