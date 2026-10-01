/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2026
 *  -- Removed subresource state tracking (D3D12_RESOURCE_STATES): RHI::Image records it.
 *  -- Subresource layouts moved to RHI::Image, which computes them from DeviceLimits.
 */

#include "Image.h"

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Conversions.h>
#include <DX12.h>
#include <RHI/Resource/Image/ImageEnums.h>

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
}
