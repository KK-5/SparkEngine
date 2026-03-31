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
 *  -- InitSubresourceAttachmentState / GetSubresourceIndexByRange: aspect flags -> plane slices via GetImageAspectFlags(format).
 *  -- GetAttachmentStateByRange: per-plane segments; merge Depth+Stencil to one DepthStencil when state and mip/array match.
 */

#include "Image.h"

#include <EASTL/algorithm.h>

#include <Log/SpdLogSystem.h>
#include <Conversions.h>
#include <DX12.h>
#include <RHI/Resource/Image/ImageEnums.h>

namespace Spark::RHI::DX12
{
    namespace
    {
        RHI::ImageAspectFlags AspectFlagsForPlaneSlice(const RHI::ImageDescriptor& desc, uint32_t planeSlice)
        {
            const RHI::ImageAspectFlags formatAspects = GetImageAspectFlags(desc.m_format);
            if (CheckBitsAll(formatAspects, RHI::ImageAspectFlags::DepthStencil))
            {
                return planeSlice == 0 ? RHI::ImageAspectFlags::Depth : RHI::ImageAspectFlags::Stencil;
            }
            if (CheckBitsAny(formatAspects, RHI::ImageAspectFlags::Depth))
            {
                return RHI::ImageAspectFlags::Depth;
            }
            return RHI::ImageAspectFlags::Color;
        }

        bool RangesMatchMipArray(const RHI::ImageSubresourceRange& a, const RHI::ImageSubresourceRange& b)
        {
            return a.m_mipSliceMin == b.m_mipSliceMin && a.m_mipSliceMax == b.m_mipSliceMax &&
                a.m_arraySliceMin == b.m_arraySliceMin && a.m_arraySliceMax == b.m_arraySliceMax;
        }

        void AppendRunSegmentsMergedIfSameStateAndMipArray(
            eastl::vector<Image::SubresourceRangeAttachmentState>& runSegments,
            uint32_t d3dPlaneCount,
            eastl::vector<Image::SubresourceRangeAttachmentState>& outResult)
        {
            if (runSegments.size() == 2u && d3dPlaneCount == 2u &&
                runSegments[0].m_state == runSegments[1].m_state &&
                RangesMatchMipArray(runSegments[0].m_range, runSegments[1].m_range))
            {
                const auto& a = runSegments[0];
                const auto& b = runSegments[1];
                const bool depthThenStencil =
                    a.m_range.m_aspectFlags == RHI::ImageAspectFlags::Depth &&
                    b.m_range.m_aspectFlags == RHI::ImageAspectFlags::Stencil;
                const bool stencilThenDepth =
                    a.m_range.m_aspectFlags == RHI::ImageAspectFlags::Stencil &&
                    b.m_range.m_aspectFlags == RHI::ImageAspectFlags::Depth;
                if (depthThenStencil || stencilThenDepth)
                {
                    RHI::ImageSubresourceRange merged = a.m_range;
                    merged.m_aspectFlags = RHI::ImageAspectFlags::DepthStencil;
                    outResult.emplace_back(Image::SubresourceRangeAttachmentState{ merged, a.m_state });
                    return;
                }
            }
            outResult.insert(outResult.end(), runSegments.begin(), runSegments.end());
        }
    }

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

    D3D12_RESOURCE_STATES Image::GetInitialResourceState() const
    {
        return m_initialResourceState;
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

        const RHI::ImageDescriptor& desc = GetDescriptor();
        RHI::ImageAspectFlags aspectFlags = subRange.m_aspectFlags;

        if (aspectFlags == RHI::ImageAspectFlags::All || aspectFlags == RHI::ImageAspectFlags::None)
        {
            aspectFlags = GetImageAspectFlags(desc.m_format);
        }

        const uint32_t mipLevels = desc.m_mipLevels;
        const uint32_t arraySize = desc.m_arraySize;

        uint32_t minIndex = UINT32_MAX;
        uint32_t maxIndex = 0;

        auto accumulatePlaneRange = [&](uint16_t planeSlice)
        {
            const uint32_t s = D3D12CalcSubresource(
                subRange.m_mipSliceMin,
                subRange.m_arraySliceMin,
                planeSlice,
                mipLevels,
                arraySize);
            const uint32_t e = D3D12CalcSubresource(
                subRange.m_mipSliceMax,
                subRange.m_arraySliceMax,
                planeSlice,
                mipLevels,
                arraySize);
            minIndex = eastl::min(minIndex, s);
            maxIndex = eastl::max(maxIndex, e);
        };

        if (CheckBitsAny(aspectFlags, RHI::ImageAspectFlags::Color))
        {
            accumulatePlaneRange(ConvertImageAspectToPlaneSlice(RHI::ImageAspect::Color));
        }
        if (CheckBitsAny(aspectFlags, RHI::ImageAspectFlags::Depth))
        {
            accumulatePlaneRange(ConvertImageAspectToPlaneSlice(RHI::ImageAspect::Depth));
        }
        if (CheckBitsAny(aspectFlags, RHI::ImageAspectFlags::Stencil))
        {
            accumulatePlaneRange(ConvertImageAspectToPlaneSlice(RHI::ImageAspect::Stencil));
        }

        if (minIndex == UINT32_MAX)
        {
            accumulatePlaneRange(0);
        }

        indexStart = minIndex;
        indexEnd = maxIndex;
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

        const RHI::ImageDescriptor& desc = GetDescriptor();
        const uint32_t mipLevels = desc.m_mipLevels;
        const uint32_t arraySize = desc.m_arraySize;
        const uint32_t planeSize = mipLevels * arraySize;
        const uint32_t d3dPlaneCount =
            CheckBitsAll(GetImageAspectFlags(desc.m_format), RHI::ImageAspectFlags::DepthStencil) ? 2u : 1u;

        eastl::vector<SubresourceRangeAttachmentState> result;
        D3D12_RESOURCE_STATES curState = m_subresourceState[indexStart];
        uint32_t rangeStart = indexStart;
        for (uint32_t index = indexStart; index <= indexEnd + 1; ++index)
        {
            if (index == indexEnd + 1 || m_subresourceState[index] != curState)
            {
                const uint32_t runEnd = index - 1;

                eastl::vector<SubresourceRangeAttachmentState> runSegments;
                for (uint32_t planeSlice = 0; planeSlice < d3dPlaneCount; ++planeSlice)
                {
                    const uint32_t planeIndexStart = planeSlice * planeSize;
                    const uint32_t planeIndexEnd = (planeSlice + 1) * planeSize - 1;
                    const uint32_t segStart = eastl::max(rangeStart, planeIndexStart);
                    const uint32_t segEnd = eastl::min(runEnd, planeIndexEnd);
                    if (segStart > segEnd)
                    {
                        continue;
                    }

                    uint16_t mipA;
                    uint16_t arrayA;
                    uint16_t planeA;
                    uint16_t mipB;
                    uint16_t arrayB;
                    uint16_t planeB;
                    D3D12DecomposeSubresource(segStart, mipLevels, arraySize, mipA, arrayA, planeA);
                    D3D12DecomposeSubresource(segEnd, mipLevels, arraySize, mipB, arrayB, planeB);
                    (void)planeA;
                    (void)planeB;

                    const uint16_t mipSliceMin = eastl::min(mipA, mipB);
                    const uint16_t mipSliceMax = eastl::max(mipA, mipB);
                    const uint16_t arraySliceMin = eastl::min(arrayA, arrayB);
                    const uint16_t arraySliceMax = eastl::max(arrayA, arrayB);

                    RHI::ImageSubresourceRange subRange(mipSliceMin, mipSliceMax, arraySliceMin, arraySliceMax);
                    subRange.m_aspectFlags = AspectFlagsForPlaneSlice(desc, planeSlice);

                    runSegments.emplace_back(SubresourceRangeAttachmentState{ subRange, curState });
                }

                AppendRunSegmentsMergedIfSameStateAndMipArray(runSegments, d3dPlaneCount, result);

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


    /*
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
    */

    void Image::InitSubresourceAttachmentState()
    {
        const RHI::ImageDescriptor desc = GetDescriptor();
        const RHI::ImageAspectFlags aspectFlags = GetImageAspectFlags(desc.m_format);

        // D3D12 subresource index = mip + array * MipLevels + planeSlice * MipLevels * ArraySize.
        // Plane 1 indices start at mipLevels * arraySize; the buffer must hold mipLevels * arraySize * (D3D plane count).
        // Use format plane count, not the number of aspect bits mapped to distinct planes (those can coincide on plane 0).
        const uint32_t d3dPlaneCount =
            CheckBitsAll(aspectFlags, RHI::ImageAspectFlags::DepthStencil) ? 2u : 1u;

        const uint32_t subresourceSize = desc.m_mipLevels * desc.m_arraySize * d3dPlaneCount;
        m_subresourceState.resize(subresourceSize);

        for (uint32_t planeSlice = 0; planeSlice < d3dPlaneCount; ++planeSlice)
        {
            for (uint16_t arraySlice = 0; arraySlice < desc.m_arraySize; ++arraySlice)
            {
                for (uint16_t mipSlice = 0; mipSlice < desc.m_mipLevels; ++mipSlice)
                {
                    const uint32_t subresourceIndex = D3D12CalcSubresource(
                        mipSlice,
                        arraySlice,
                        static_cast<UINT>(planeSlice),
                        desc.m_mipLevels,
                        desc.m_arraySize);

                    ASSERT(subresourceIndex < subresourceSize, "[Image] Subresource index out of range");
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