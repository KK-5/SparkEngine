/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- All of subresource state are stored in vector<D3D12_RESOURCE_STATES>
 */

#pragma once

#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <RHI/Device/DeviceObjectFactory.h>
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

        // Returns the memory view allocated to this image.
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

        struct SubresourceRangeAttachmentState
        {
            RHI::ImageSubresourceRange m_range;
            D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
        };

        // Set the attachment state of the image subresources. If argument "range" is nullptr, then the new state will be applied to all subresources.
        void SetAttachmentState(D3D12_RESOURCE_STATES state, const RHI::ImageSubresourceRange* range = nullptr);

        // Set the attachment state of the image subresources using the subresource index.
        void SetAttachmentState(D3D12_RESOURCE_STATES state, uint32_t subresourceIndex);

        // Get the attachment state of some of the subresources of the image by their RHI::ImageSubresourceRange.
        // If argument "range" is nullptr, then the state for all subresource will be return.
        eastl::vector<SubresourceRangeAttachmentState> GetAttachmentStateByRange(const RHI::ImageSubresourceRange* range = nullptr) const;

        // Get the attachment state of some of the subresources of the image by their subresource index.
        // If argument "range" is nullptr, then the state for all subresource will be return.
        // eastl::vector<SubresourceAttachmentState> GetAttachmentStateByIndex(const RHI::ImageSubresourceRange* range = nullptr) const;

        // Return the initial state of this image (the one used when it was created).
        D3D12_RESOURCE_STATES GetInitialResourceState() const;

    private:
        Image() = default;

        friend class SwapChain;
        friend class ImagePool;
        friend class StreamingImagePool;
        friend class TransientResourcePool;
        friend class DeviceObjectFactory<Image>;

        //////////////////////////////////////////////////////////////////////////
        // RHI::Image
        void GetSubresourceLayoutsInternal(
            const RHI::ImageSubresourceRange& subresourceRange,
            RHI::ImageSubresourceLayout* subresourceLayouts,
            size_t* totalSizeInBytes) const override;
                            
        bool IsStreamableInternal() const override;
        //////////////////////////////////////////////////////////////////////////

        // Calculate the size of all the tiles allocated for this image and save the number in m_residentSizeInBytes
        void UpdateResidentTilesSizeInBytes(uint32_t sizePerTile);

        void GetSubresourceIndexByRange(const RHI::ImageSubresourceRange* range, uint32_t& indexStart, uint32_t& indexEnd) const;

        void GenerateSubresourceLayouts();

        void InitSubresourceAttachmentState();
        
        // The memory view allocated to this image.
        MemoryView m_memoryView;

        // The number of bytes actually resident.
        // For tiled resources, this size is same as the memory of tiles are used for mipmaps which are resident. It would be updated every time the image's mipmap
        // is expanded or trimmed.
        // For committed resources, this size won't change after image is initialized. 
        size_t m_residentSizeInBytes = 0;

        // The minimum resident size of this image. The size is the same as resident size when image was initialized.
        size_t m_minimumResidentSizeInBytes = 0;

        eastl::array<RHI::ImageSubresourceLayout, RHI::Limits::Image::MipCountMax> m_subresourceLayoutsPerMipChain;

        // The layout of tiles with respect to each subresource in the image.
        ImageTileLayout m_tileLayout;

        // The map of heap tiles allocated for each subresources
        // Note: the tiles allocated for each subresource may come from multiple heap pages 
        // eastl::unordered_map<uint32_t, eastl::vector<HeapTiles>> m_heapTiles;

        // Tracking the actual mip level data uploaded. It's also used for invalidate image view. 
        uint32_t m_streamedMipLevel = 0;

        // The initial state for the graph compiler to use when compiling the resource transition chain.
        // eastl::vector<SubresourceRangeAttachmentState> m_attachmentState;

        eastl::vector<D3D12_RESOURCE_STATES> m_subresourceState;

        // The initial state used when creating this image.
        D3D12_RESOURCE_STATES m_initialResourceState = D3D12_RESOURCE_STATE_COMMON;

        // The number of resolve operations pending for this image.
        eastl::atomic<uint32_t> m_pendingResolves = 0;
    };
}