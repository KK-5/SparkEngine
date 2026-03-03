/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- 
 */

#include "CommandPool.h"

#include <Log/SpdLogSystem.h>

#include <Device/Device.h>
#include <ID3D12Factory.h>
#include <Conversions.h>
#include "CommandList.h"

namespace Spark::RHI::DX12
{
    void CommandAllocatorFactory::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    ID3D12CommandAllocator* CommandAllocatorFactory::Allocate()
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hr = m_descriptor.m_dx12Device->CreateCommandAllocator(
            ConvertHardwareQueueClass(m_descriptor.m_hardwareQueueClass),
            IID_PPV_ARGS(allocator.GetAddressOf()));

        return allocator.Get();
    }

    void CommandAllocatorFactory::ReAllocate(ID3D12CommandAllocator& allocator)
    {
        allocator.Reset();
    }

    void CommandListFactory::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    CommandList* CommandListFactory::Allocate(ID3D12CommandAllocator* commandAllocator)
    {
        Ptr<CommandList> commandList = Service<RHI::Factory>::Get()->CreateCommandList();
        commandList->Init(*m_descriptor.m_device, m_descriptor.m_hardwareQueueClass, commandAllocator);
        return commandList.get();
    }

    void CommandListFactory::ReAllocate(CommandList& commandList, ID3D12CommandAllocator* commandAllocator)
    {
        commandList.Reset(commandAllocator);
    }

    void DeAllocate(CommandList& commandList, [[maybe_unused]] bool isPoolShutdown)
    {
        commandList.Shutdown();
    }

    bool RecycleObject([[maybe_unused]] CommandList& commandList)
    {
        return true;
    }

    void CommandListAllocator::Init(const Descriptor& descriptor, Device* device)
    {
        ASSERT(m_isInitialized == false, "CommandListAllocator already initialized!");

        for (uint32_t queueIdx = 0; queueIdx < RHI::HardwareQueueClassCount; ++queueIdx)
        {
            CommandListPool& commandListPool = m_commandListPools[queueIdx];
            CommandAllocatorPool& commandAllocatorPool = m_commandAllocatorPools[queueIdx];

            CommandListPool::Descriptor commandListPoolDescriptor;
            commandListPoolDescriptor.m_device = descriptor.m_device;
            commandListPoolDescriptor.m_hardwareQueueClass = static_cast<RHI::HardwareQueueClass>(queueIdx);
            commandListPoolDescriptor.m_collectLatency = descriptor.m_frameCountMax;
            commandListPool.Init(commandListPoolDescriptor);

            CommandAllocatorPool::Descriptor commandAllocatorPoolDescriptor;
            commandAllocatorPoolDescriptor.m_hardwareQueueClass = static_cast<RHI::HardwareQueueClass>(queueIdx);
            commandAllocatorPoolDescriptor.m_dx12Device = descriptor.m_device->GetDevice();
            commandAllocatorPoolDescriptor.m_collectLatency = descriptor.m_frameCountMax;
            commandAllocatorPool.Init(commandAllocatorPoolDescriptor);
        }

        m_isInitialized = true;
    }

    void CommandListAllocator::Shutdown()
    {
        if (m_isInitialized)
        {
            for (uint32_t queueIdx = 0; queueIdx < RHI::HardwareQueueClassCount; ++queueIdx)
            {
                m_activeLists.clear();
                m_commandListPools[queueIdx].Shutdown();
                m_commandAllocatorPools[queueIdx].Shutdown();
            }
            m_isInitialized = false;
        }
    }

    CommandList* CommandListAllocator::Allocate(RHI::HardwareQueueClass hardwareQueueClass)
    {
        ASSERT(m_isInitialized, "CommandListAllocator is not initialized!");
        return m_commandListPools[static_cast<uint32_t>(hardwareQueueClass)].CreateObject().get();
    }

    void CommandListAllocator::Collect()
    {
        for (uint32_t queueIdx = 0; queueIdx < RHI::HardwareQueueClassCount; ++queueIdx)
        {
            m_commandListPools[queueIdx].Collect();
            m_commandAllocatorPools[queueIdx].Collect();
        }
    }
}