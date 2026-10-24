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

        // It shares its memory with the reserved buffer, so it is not to be used as a buffer.
        if (backing->GetDescriptor().m_reserved || backing->GetDescriptor().m_bindFlags != BufferBindFlags::None)
        {
            LOG_ERROR("[CommandQueue] Backing buffer {} of {} must be neither reserved nor bound as anything.",
                      backing->GetName().GetCStr(), buffer->GetName().GetCStr());
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

    RHI::HardwareQueueClass CommandQueue::GetHardwareQueueClass() const
    {
        return m_descriptor.m_hardwareQueueClass;
    }

    const CommandQueueDescriptor& CommandQueue::GetDescriptor() const
    {
        return m_descriptor;
    }
}