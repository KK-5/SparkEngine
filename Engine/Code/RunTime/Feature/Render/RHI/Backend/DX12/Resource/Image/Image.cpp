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

#include "Image.h"

#include <Log/SpdLogSystem.h>
#include <Conversions.h>
#include <DX12.h>

namespace Spark::RHI::DX12
{
    bool ImageTileLayout::IsPacked(uint32_t subresourceIndex) const
    {
        return m_subresourceTiling[subresourceIndex].StartTileIndexInOverallResource == D3D12_PACKED_TILE;
    }

    uint32_t ImageTileLayout::GetPackedSubresourceIndex() const
    {
        return m_mipCountStandard;
    }

    uint32_t ImageTileLayout::GetTileOffset(uint32_t subresourceIndex) const
    {
        uint32_t tileOffset = m_subresourceTiling[subresourceIndex].StartTileIndexInOverallResource;
        return tileOffset != D3D12_PACKED_TILE ? tileOffset : m_tileCountStandard;
    }

    void ImageTileLayout::GetSubresourceTileInfo(uint32_t subresourceIndex, uint32_t& imageTileOffset, D3D12_TILED_RESOURCE_COORDINATE& coordinate, D3D12_TILE_REGION_SIZE& regionSize) const
    {
        if (IsPacked(subresourceIndex))
        {
            // Packed mips are only supported when the array count is 1. The subresource is
            // equal to the first non-standard mip.
            coordinate = CD3DX12_TILED_RESOURCE_COORDINATE(0, 0, 0, m_mipCountStandard);

            // The region is a flat list of tiles.
            regionSize = CD3DX12_TILE_REGION_SIZE(m_tileCountPacked, 0, 0, 0, 0);

            // Assign the offset of the tile relative to the image.
            imageTileOffset = m_tileCountStandard;
        }
        else
        {
            coordinate = CD3DX12_TILED_RESOURCE_COORDINATE(0, 0, 0, subresourceIndex);

            // The region is a box covering all the tiles in the subresource.
            const D3D12_SUBRESOURCE_TILING& tiling = m_subresourceTiling[subresourceIndex];
            regionSize = CD3DX12_TILE_REGION_SIZE(
                tiling.WidthInTiles * tiling.HeightInTiles * tiling.DepthInTiles, TRUE,
                tiling.WidthInTiles, tiling.HeightInTiles, tiling.DepthInTiles);

            imageTileOffset = tiling.StartTileIndexInOverallResource;
        }
    }

    const MemoryView& Image::GetMemoryView() const
    {
        return m_memoryView;
    }

    MemoryView& Image::GetMemoryView()
    {
        return m_memoryView;
    }

    bool Image::IsTiled() const
    {
        return m_tileLayout.m_tileCount > 0;
    }

    // Get mip level uploaded to GPU
    uint32_t Image::GetStreamedMipLevel() const
    {
        return m_streamedMipLevel;
    }

    void Image::SetStreamedMipLevel(uint32_t streamedMipLevel)
    {
        if (m_streamedMipLevel != streamedMipLevel)
        {
            m_streamedMipLevel = streamedMipLevel;
            // InvalidateViews();
        }
    }

    void Image::GetSubresourceIndexByRange(const RHI::ImageSubresourceRange* range, uint32_t& indexStart, uint32_t& indexEnd) const
    {
        RHI::ImageSubresourceRange subRange(GetDescriptor());
        if (range)
        {
            subRange = *range;
        }

        uint32_t planeSlice = ConvertImageAspectToPlaneSlice(static_cast<RHI::ImageAspect>(subRange.m_aspectFlags));
        indexStart = D3D12CalcSubresource(
            subRange.m_mipSliceMin,
            subRange.m_arraySliceMin,
            planeSlice,
            GetDescriptor().m_mipLevels,
            GetDescriptor().m_arraySize
        );

        indexEnd = D3D12CalcSubresource(
            subRange.m_mipSliceMax,
            subRange.m_arraySliceMax,
            planeSlice,
            GetDescriptor().m_mipLevels,
            GetDescriptor().m_arraySize
        );
    }

    void Image::SetAttachmentState(D3D12_RESOURCE_STATES state, const RHI::ImageSubresourceRange* range)
    {
        uint32_t indexStart = 0;
        uint32_t indexEnd = 0;

        GetSubresourceIndexByRange(range, indexStart, indexEnd);

        for (uint32_t index = indexStart; index <= indexEnd; ++index)
        {
            m_subresourceState[index] = state;
        }

    }

    void Image::SetAttachmentState(D3D12_RESOURCE_STATES state, uint32_t subresourceIndex)
    {
        if (subresourceIndex == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            eastl::fill(m_subresourceState.begin(), m_subresourceState.end(), state);
        }
        else
        {
            ASSERT(subresourceIndex < m_subresourceState.size(), "[Image] Invalid subresourceIndex {}", subresourceIndex);
            m_subresourceState[subresourceIndex] = state;
        }
    }

    eastl::vector<Image::SubresourceRangeAttachmentState> Image::GetAttachmentStateByRange(const RHI::ImageSubresourceRange* range) const
    {
        uint32_t indexStart = 0;
        uint32_t indexEnd = 0;

        GetSubresourceIndexByRange(range, indexStart, indexEnd);

        eastl::vector<SubresourceRangeAttachmentState> result;
        D3D12_RESOURCE_STATES curState = m_subresourceState[indexStart];
        uint32_t rangeStart = indexStart;
        for(uint32_t index = indexStart; index <= indexEnd + 1; ++index)
        {
            if (index == indexEnd + 1 || m_subresourceState[index] != curState)
            {
                uint32_t rangeEnd = index - 1;
                uint16_t mipSliceMin;
                uint16_t arraySliceMin;
                uint16_t planeSliceMin;
                D3D12DecomposeSubresource(
                    rangeStart, GetDescriptor().m_mipLevels, GetDescriptor().m_arraySize, mipSliceMin, arraySliceMin, planeSliceMin);
                
                uint16_t mipSliceMax;
                uint16_t arraySliceMax;
                uint16_t planeSliceMax;
                D3D12DecomposeSubresource(
                    rangeEnd, GetDescriptor().m_mipLevels, GetDescriptor().m_arraySize, mipSliceMax, arraySliceMax, planeSliceMax);
                
                if (planeSliceMin == planeSliceMax)
                {
                    RHI::ImageSubresourceRange range(mipSliceMin, mipSliceMax, arraySliceMin, arraySliceMax);
                    range.m_aspectFlags = ConvertPlaneSliceToImageAspectFlags(planeSliceMin);
                    result.emplace_back(SubresourceRangeAttachmentState{range, curState});
                }
                else
                {
                    RHI::ImageSubresourceRange rangeMin(mipSliceMin, GetDescriptor().m_mipLevels - 1, arraySliceMin, GetDescriptor().m_arraySize - 1);
                    rangeMin.m_aspectFlags = ConvertPlaneSliceToImageAspectFlags(planeSliceMin);
                    result.emplace_back(SubresourceRangeAttachmentState{rangeMin, curState});

                    RHI::ImageSubresourceRange rangeMax(0, mipSliceMax, 0, arraySliceMax);
                    rangeMax.m_aspectFlags = ConvertPlaneSliceToImageAspectFlags(planeSliceMax);
                    result.emplace_back(SubresourceRangeAttachmentState{rangeMax, curState});
                }
                rangeStart = index;
                if (index < indexEnd + 1)
                {
                    curState = m_subresourceState[rangeStart];
                }
            }
        }

        return result;
    }

    void Image::GetSubresourceLayoutsInternal(
            const RHI::ImageSubresourceRange& subresourceRange,
            RHI::ImageSubresourceLayout* subresourceLayouts,
            size_t* totalSizeInBytes) const
    {
        const RHI::ImageDescriptor& imageDescriptor = GetDescriptor();
        uint32_t byteOffset = 0;

        if (subresourceLayouts)
        {
            for (uint16_t arraySlice = subresourceRange.m_arraySliceMin; arraySlice <= subresourceRange.m_arraySliceMax; ++arraySlice)
            {
                for (uint16_t mipSlice = subresourceRange.m_mipSliceMin; mipSlice <= subresourceRange.m_mipSliceMax; ++mipSlice)
                {
                    const RHI::ImageSubresourceLayout& subresourceLayout = m_subresourceLayoutsPerMipChain[mipSlice];
                    const uint32_t subresourceIndex = RHI::GetImageSubresourceIndex(mipSlice, arraySlice, imageDescriptor.m_mipLevels);
                    subresourceLayouts[subresourceIndex] = subresourceLayout;
                    subresourceLayouts[subresourceIndex].m_offset = byteOffset;
                    byteOffset = AlignUp(byteOffset + subresourceLayout.m_bytesPerImage * subresourceLayout.m_size.m_depth, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
                }
            }
        }
        else
        {
            for (uint16_t arraySlice = subresourceRange.m_arraySliceMin; arraySlice <= subresourceRange.m_arraySliceMax; ++arraySlice)
            {
                for (uint16_t mipSlice = subresourceRange.m_mipSliceMin; mipSlice <= subresourceRange.m_mipSliceMax; ++mipSlice)
                {
                    const RHI::ImageSubresourceLayout& subresourceLayout = m_subresourceLayoutsPerMipChain[mipSlice];
                    byteOffset = AlignUp(byteOffset + subresourceLayout.m_bytesPerImage * subresourceLayout.m_size.m_depth, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
                }
            }
        }

        if (totalSizeInBytes)
        {
            *totalSizeInBytes = byteOffset;
        }
    }

    void Image::GenerateSubresourceLayouts()
    {
        for (uint16_t mipSlice = 0; mipSlice < GetDescriptor().m_mipLevels; ++mipSlice)
        {
            RHI::ImageSubresourceLayout& subresourceLayout = m_subresourceLayoutsPerMipChain[mipSlice];

            RHI::ImageSubresource subresource;
            subresource.m_mipSlice = mipSlice;
            subresourceLayout = RHI::GetImageSubresourceLayout(GetDescriptor(), subresource);

            // Align the row size to match the DX12 row pitch alignment.
            subresourceLayout.m_bytesPerRow = AlignUp(subresourceLayout.m_bytesPerRow, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            subresourceLayout.m_bytesPerImage = subresourceLayout.m_rowCount * subresourceLayout.m_bytesPerRow;
        }
    }

    bool Image::IsStreamableInternal() const
    {
        return IsTiled();
    }

    void Image::SetDescriptor(const RHI::ImageDescriptor& descriptor)
    {
        RHI::Image::SetDescriptor(descriptor);

        m_initialResourceState = D3D12_RESOURCE_STATE_COMMON;

        const RHI::ImageBindFlags bindFlags = descriptor.m_bindFlags;

        // Write only states
        const bool renderTarget = CheckBitsAny(bindFlags, RHI::ImageBindFlags::Color);
        const bool copyDest = CheckBitsAny(bindFlags, RHI::ImageBindFlags::CopyWrite);
        const bool depthTarget = CheckBitsAny(bindFlags, RHI::ImageBindFlags::DepthStencil);

        // Read Only States
        const bool shaderResource = CheckBitsAny(bindFlags, RHI::ImageBindFlags::ShaderRead);
        const bool copySource = CheckBitsAny(bindFlags, RHI::ImageBindFlags::CopyRead);

        const bool writeState = renderTarget || copyDest || depthTarget;
        const bool readState = shaderResource || copySource;

        // If any write only state is set, only write only resource states can be applied
        if (writeState)
        {
            if (renderTarget)
            {
                m_initialResourceState |= D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            else if (copyDest)
            {
                m_initialResourceState |= D3D12_RESOURCE_STATE_COPY_DEST;
            }
            else if (depthTarget)
            {
                m_initialResourceState |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
            }
        }
        // If any read only state is set, only read only resource states can be applied
        else if (readState)
        {
            if (shaderResource)
            {
                if (CheckBitsAny(descriptor.m_sharedQueueMask, RHI::HardwareQueueClassMask::Graphics))
                {
                    m_initialResourceState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                }
                if (CheckBitsAny(descriptor.m_sharedQueueMask, RHI::HardwareQueueClassMask::Compute))
                {
                    m_initialResourceState |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                }
            }

            if (copySource)
            {
                m_initialResourceState |= D3D12_RESOURCE_STATE_COPY_SOURCE;
            }
        }
        // If neither a read only or write only state is set, we can set a read/write state
        else
        {
            if (CheckBitsAny(bindFlags, RHI::ImageBindFlags::ShaderWrite))
            {
                m_initialResourceState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }
        }

        InitSubresourceAttachmentState();
    }

    void Image::InitSubresourceAttachmentState()
    {
        const RHI::ImageDescriptor desc = GetDescriptor();

        uint32_t subresourceSize = 2 * desc.m_arraySize * desc.m_mipLevels; // 目前planeSlice最多为2
        m_subresourceState.reserve(subresourceSize);

        for (uint16_t aspectIndex = 0; aspectIndex < RHI::ImageAspectCount; ++aspectIndex)
        {
            uint16_t planeSlice = ConvertImageAspectToPlaneSlice(static_cast<RHI::ImageAspect>(aspectIndex));
            for (uint16_t arraySlice = 0; arraySlice < desc.m_arraySize; ++arraySlice)
            {
                for (uint16_t mipSlice = 0; mipSlice < desc.m_mipLevels; ++ mipSlice)
                {
                    uint32_t subresourceIndex = D3D12CalcSubresource(
                        mipSlice,
                        arraySlice,
                        planeSlice,
                        desc.m_mipLevels,
                        desc.m_arraySize);
                    
                    m_subresourceState[subresourceIndex] = m_initialResourceState;
                }
            }
        }
    }
    
    void Image::SetUploadFenceValue(uint64_t fenceValue)
    {
        ASSERT(fenceValue > m_uploadFenceValue, "New fence value should always larger than previous fence value");
        m_uploadFenceValue = fenceValue;
    }

    uint64_t Image::GetUploadFenceValue() const
    {
        return m_uploadFenceValue;
    }
    
}