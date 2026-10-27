/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "CommandQueue.h"

#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/Buffer.h>

namespace Spark::RHI
{
    bool CommandQueue::ValidateIsInitialized() const
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[CommandQueue] CommandQueue is not initialized!");
                return false;
            }
        }

        return true;
    }

    ResultCode CommandQueue::Init(Device& device, const CommandQueueDescriptor& descriptor)
    {
        if (IsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is already initialized!");
            return ResultCode::InvalidOperation;
        }

        const ResultCode resultCode = InitInternal(device, descriptor);

        if (resultCode == ResultCode::Success)
        {
            DeviceObject::Init(device);
                
            m_descriptor = descriptor;
        }
        return resultCode;
    }

    void CommandQueue::Shutdown()
    {
        if (ValidateIsInitialized())
        {
            WaitForIdle();
            ShutdownInternal();
            DeviceObject::Shutdown();
        }
    }

    ResultCode CommandQueue::FlushCommands(RHI::Fence& fence)
    {
        if (fence.Reset() != RHI::ResultCode::Success)
        {
            LOG_ERROR("[CommandQueue] Reset Fence failed.");
            return ResultCode::Fail;
        }

        if (Signal(fence) != ResultCode::Success)
        {
            LOG_ERROR("[CommandQueue] Signal Fence failed.");
            return ResultCode::Fail;
        }
            
        fence.WaitOnCpu();
        return ResultCode::Success;
    }

    ResultCode CommandQueue::ExecuteCommands(eastl::span<CommandList*> commandLists)
    {
        if (!ValidateIsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is not initialize.");
            return ResultCode::InvalidOperation;
        }

        ExecuteCommandsInternal(commandLists);
        return ResultCode::Success;
    }

    ResultCode CommandQueue::Signal(RHI::Fence& fence, uint64_t value)
    {
        if (!ValidateIsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is not initialize.");
            return ResultCode::InvalidOperation;
        }

        if (!fence.IsInitialized())
        {
            LOG_ERROR("[CommandQueue] Fence is not initialize.");
            return ResultCode::InvalidOperation;
        }

        fence.SetPendingValue(value);
        SignalInternal(fence, value);

        return ResultCode::Success;
    }

    ResultCode CommandQueue::Signal(RHI::Fence& fence)
    {
        return Signal(fence, fence.GetPendingValue());
    }

    ResultCode CommandQueue::Wait(RHI::Fence& fence, uint64_t value)
    {
        if (!ValidateIsInitialized())
        {
            LOG_ERROR("[CommandQueue] CommandQueue is not initialize.");
            return ResultCode::InvalidOperation;
        }

        if (!fence.IsInitialized())
        {
            LOG_ERROR("[CommandQueue] Fence is not initialize.");
            return ResultCode::InvalidOperation;
        }

        WaitInternal(fence, value);

        return ResultCode::Success;
    }

    ResultCode CommandQueue::UpdateTileMappings(eastl::span<const BufferTileMapping> mappings)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (Validation::isEnabled)
        {
            for (const BufferTileMapping& mapping : mappings)
            {
                if (!ValidateTileMapping(mapping))
                {
                    return ResultCode::InvalidArgument;
                }
            }
            if (!ValidateTileMappingsDisjoint(mappings))
            {
                return ResultCode::InvalidArgument;
            }
        }

        if (mappings.empty())
        {
            return ResultCode::Success;
        }
        return UpdateTileMappingsInternal(mappings);
    }

    bool CommandQueue::ValidateTileMapping(const BufferTileMapping& mapping) const
    {
        const uint64_t pageSize = GetDevice().GetLimits().m_reservedPageSizeInBytes;
        const Buffer*  buffer = mapping.m_buffer;
        if (!buffer || !buffer->GetDescriptor().m_reserved)
        {
            LOG_ERROR("[CommandQueue] Tile mapping target {} is not a reserved buffer.",
                      buffer ? buffer->GetName().GetCStr() : "[Null]");
            return false;
        }

        if (mapping.m_byteCount == 0 || mapping.m_byteOffset % pageSize != 0 || mapping.m_byteCount % pageSize != 0)
        {
            LOG_ERROR("[CommandQueue] Tile mapping of {} at {} for {} bytes is not a whole number of {}-byte pages.",
                      buffer->GetName().GetCStr(), mapping.m_byteOffset, mapping.m_byteCount, pageSize);
            return false;
        }

        if (mapping.m_byteOffset + mapping.m_byteCount > buffer->GetDescriptor().m_byteCount)
        {
            LOG_ERROR("[CommandQueue] Tile mapping at {} for {} bytes runs past the {} bytes of reserved buffer {}.",
                      mapping.m_byteOffset, mapping.m_byteCount, buffer->GetDescriptor().m_byteCount,
                      buffer->GetName().GetCStr());
            return false;
        }

        const Buffer* backing = mapping.m_backingBuffer;
        if (!backing)
        {
            return true;
        }

        // Its memory was chosen for reserved buffers of the flags it names.
        if (backing->GetDescriptor().m_bindFlags != (buffer->GetDescriptor().m_bindFlags | BufferBindFlags::TileBacking))
        {
            LOG_ERROR("[CommandQueue] Backing buffer {} of {} has bind flags 0x{:x}: a tile backing of the reserved "
                      "buffer's 0x{:x} is wanted.",
                      backing->GetName().GetCStr(), buffer->GetName().GetCStr(),
                      static_cast<uint32_t>(backing->GetDescriptor().m_bindFlags),
                      static_cast<uint32_t>(buffer->GetDescriptor().m_bindFlags));
            return false;
        }

        if (mapping.m_backingByteOffset % pageSize != 0)
        {
            LOG_ERROR("[CommandQueue] Backing buffer {} offset {} is not on a {}-byte page.",
                      backing->GetName().GetCStr(), mapping.m_backingByteOffset, pageSize);
            return false;
        }

        if (mapping.m_backingByteOffset + mapping.m_byteCount > backing->GetDescriptor().m_byteCount)
        {
            LOG_ERROR("[CommandQueue] Tile mapping at {} for {} bytes runs past the {} bytes of backing buffer {}.",
                      mapping.m_backingByteOffset, mapping.m_byteCount, backing->GetDescriptor().m_byteCount,
                      backing->GetName().GetCStr());
            return false;
        }

        return true;
    }

    bool CommandQueue::ValidateTileMappingsDisjoint(eastl::span<const BufferTileMapping> mappings) const
    {
        eastl::vector<const BufferTileMapping*> sorted;
        sorted.reserve(mappings.size());
        for (const BufferTileMapping& mapping : mappings)
        {
            sorted.push_back(&mapping);
        }
        eastl::sort(sorted.begin(), sorted.end(), [](const BufferTileMapping* a, const BufferTileMapping* b)
        {
            return a->m_buffer != b->m_buffer ? a->m_buffer < b->m_buffer : a->m_byteOffset < b->m_byteOffset;
        });

        for (size_t i = 1; i < sorted.size(); ++i)
        {
            const BufferTileMapping& previous = *sorted[i - 1];
            const BufferTileMapping& current  = *sorted[i];
            if (previous.m_buffer == current.m_buffer
                && previous.m_byteOffset + previous.m_byteCount > current.m_byteOffset)
            {
                LOG_ERROR("[CommandQueue] Tile mappings of {} at {} and at {} overlap in one call.",
                          current.m_buffer->GetName().GetCStr(), previous.m_byteOffset, current.m_byteOffset);
                return false;
            }
        }
        return true;
    }

    ResultCode CommandQueue::UpdateTileMappings(eastl::span<const ImageTileMapping> mappings)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (Validation::isEnabled)
        {
            for (const ImageTileMapping& mapping : mappings)
            {
                if (!ValidateTileMapping(mapping))
                {
                    return ResultCode::InvalidArgument;
                }
            }
            if (!ValidateTileMappingsDisjoint(mappings))
            {
                return ResultCode::InvalidArgument;
            }
        }

        if (mappings.empty())
        {
            return ResultCode::Success;
        }
        return UpdateTileMappingsInternal(mappings);
    }

    namespace
    {
        //! Tiles of `shape` it takes to cover `size`.
        Size TileGrid(const Size& size, const Size& shape)
        {
            return Size(
                (size.m_width  + shape.m_width  - 1) / shape.m_width,
                (size.m_height + shape.m_height - 1) / shape.m_height,
                (size.m_depth  + shape.m_depth  - 1) / shape.m_depth);
        }

        uint64_t TileCount(const Size& extent)
        {
            return static_cast<uint64_t>(extent.m_width) * extent.m_height * extent.m_depth;
        }
    }

    bool CommandQueue::ValidateTileMapping(const ImageTileMapping& mapping) const
    {
        const Image* image = mapping.m_image;
        if (!image || !image->GetDescriptor().m_reserved)
        {
            LOG_ERROR("[CommandQueue] Tile mapping target {} is not a reserved image.",
                      image ? image->GetName().GetCStr() : "[Null]");
            return false;
        }

        const ImageDescriptor& descriptor = image->GetDescriptor();
        if (mapping.m_mipSlice >= descriptor.m_mipLevels || mapping.m_arraySlice >= descriptor.m_arraySize)
        {
            LOG_ERROR("[CommandQueue] Tile mapping of mip {} of slice {} is outside reserved image {}.",
                      mapping.m_mipSlice, mapping.m_arraySlice, image->GetName().GetCStr());
            return false;
        }

        const Size&   shape  = image->GetTileShape();
        const Size    grid   = TileGrid(descriptor.m_size.GetReducedMip(mapping.m_mipSlice), shape);
        const Origin& origin = mapping.m_tileOrigin;
        const Size&   extent = mapping.m_tileExtent;
        if (TileCount(extent) == 0
            || origin.m_left  + extent.m_width  > grid.m_width
            || origin.m_top   + extent.m_height > grid.m_height
            || origin.m_front + extent.m_depth  > grid.m_depth)
        {
            LOG_ERROR("[CommandQueue] Tile mapping of {}x{}x{} tiles at ({}, {}, {}) is outside the {}x{}x{} tiles "
                      "of mip {} of reserved image {}.",
                      extent.m_width, extent.m_height, extent.m_depth, origin.m_left, origin.m_top, origin.m_front,
                      grid.m_width, grid.m_height, grid.m_depth, mapping.m_mipSlice, image->GetName().GetCStr());
            return false;
        }

        const Image* backing = mapping.m_backingImage;
        if (!backing)
        {
            return true;
        }

        // Its memory was chosen for reserved images of the format and flags it names.
        const ImageDescriptor& backingDescriptor = backing->GetDescriptor();
        if (backingDescriptor.m_format != descriptor.m_format
            || backingDescriptor.m_bindFlags != (descriptor.m_bindFlags | ImageBindFlags::TileBacking))
        {
            LOG_ERROR("[CommandQueue] Backing image {} of {} has format {} and bind flags 0x{:x}: a tile backing of "
                      "the reserved image's format {} and bind flags 0x{:x} is wanted.",
                      backing->GetName().GetCStr(), image->GetName().GetCStr(),
                      static_cast<uint32_t>(backingDescriptor.m_format),
                      static_cast<uint32_t>(backingDescriptor.m_bindFlags),
                      static_cast<uint32_t>(descriptor.m_format), static_cast<uint32_t>(descriptor.m_bindFlags));
            return false;
        }

        // Of the same format, so cut into the same tiles.
        const uint64_t backingTiles = TileCount(TileGrid(backingDescriptor.m_size, shape)) * backingDescriptor.m_arraySize;
        if (mapping.m_backingTileOffset + TileCount(extent) > backingTiles)
        {
            LOG_ERROR("[CommandQueue] Tile mapping of {} tiles from tile {} runs past the {} tiles of backing image {}.",
                      TileCount(extent), mapping.m_backingTileOffset, backingTiles, backing->GetName().GetCStr());
            return false;
        }

        return true;
    }

    bool CommandQueue::ValidateTileMappingsDisjoint(eastl::span<const ImageTileMapping> mappings) const
    {
        auto overlap = [](uint32_t a, uint32_t aCount, uint32_t b, uint32_t bCount)
        {
            return a < b + bCount && b < a + aCount;
        };

        for (size_t i = 0; i < mappings.size(); ++i)
        {
            const ImageTileMapping& a = mappings[i];
            for (size_t j = i + 1; j < mappings.size(); ++j)
            {
                const ImageTileMapping& b = mappings[j];
                if (a.m_image == b.m_image && a.m_mipSlice == b.m_mipSlice && a.m_arraySlice == b.m_arraySlice
                    && overlap(a.m_tileOrigin.m_left,  a.m_tileExtent.m_width,  b.m_tileOrigin.m_left,  b.m_tileExtent.m_width)
                    && overlap(a.m_tileOrigin.m_top,   a.m_tileExtent.m_height, b.m_tileOrigin.m_top,   b.m_tileExtent.m_height)
                    && overlap(a.m_tileOrigin.m_front, a.m_tileExtent.m_depth,  b.m_tileOrigin.m_front, b.m_tileExtent.m_depth))
                {
                    LOG_ERROR("[CommandQueue] Tile mappings of mip {} of slice {} of {} overlap in one call.",
                              a.m_mipSlice, a.m_arraySlice, a.m_image->GetName().GetCStr());
                    return false;
                }
            }
        }
        return true;
    }

    RHI::HardwareQueueClass CommandQueue::GetHardwareQueueClass() const
    {
        return m_descriptor.m_hardwareQueueClass;
    }

    const CommandQueueDescriptor& CommandQueue::GetDescriptor() const
    {
        return m_descriptor;
    }
}