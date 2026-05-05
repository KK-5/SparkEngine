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

#include "CommandListPool.h"

#include <Log/SpdLogSystem.h>

#include <Device/Device.h>
#include <ID3D12Factory.h>
#include <Conversions.h>

namespace Spark::RHI::DX12
{
    void CommandAllocatorFactory::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    /*
     * ID3D12Object与SparkObject不同，SparkObject允许引用计数降为0，此时对象处于待回收状态，
     * 而ID3D12Object引用计数为0时自动析构
     *
    */
    ID3D12CommandAllocator* CommandAllocatorFactory::CreateObject()
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hr = m_descriptor.m_dx12Device->CreateCommandAllocator(
            ConvertHardwareQueueClass(m_descriptor.m_hardwareQueueClass),
            IID_PPV_ARGS(&allocator));
        // 增加引用防止析构
        allocator->AddRef();
        return allocator.Get();
    }

    void CommandAllocatorFactory::DestoryObject(ID3D12CommandAllocator* allocator, [[maybe_unused]]bool isPoolShutdown)
    {
        // 手动减少引用析构
        allocator->Release();
    }

    bool CommandAllocatorFactory::IsRecycleObject([[maybe_unused]] ID3D12CommandAllocator* allocator)
    {
        return true;
    }

    void CommandAllocatorFactory::ResetObject(ID3D12CommandAllocator* allocator)
    {
        allocator->Reset();
    }

    void CommandListFactory::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    CommandList* CommandListFactory::CreateObject(ID3D12CommandAllocator* commandAllocator)
    {
        AllocateAddress address =  m_allocator.allocate(sizeof(CommandList));
        new (address.GetAddress()) CommandList();
        CommandList* commandList = (CommandList*)address;
        commandList->Init(*m_descriptor.m_device, m_descriptor.m_hardwareQueueClass, commandAllocator);
        return commandList;
    }

    void CommandListFactory::ResetObject(CommandList* commandList, ID3D12CommandAllocator* commandAllocator)
    {
        commandList->Reset(commandAllocator);
    }

    void CommandListFactory::DestoryObject(CommandList* commandList, [[maybe_unused]] bool isPoolShutdown)
    {
        commandList->Shutdown();
        commandList->~CommandList();
        m_allocator.deallocate(commandList);
    }

    bool CommandListFactory::IsRecycleObject([[maybe_unused]] CommandList* commandList)
    {
        return true;
    }

    CommandListAllocator::ThreadSubAllocator& CommandListAllocator::GetThreadSubAllocator()
    {
        thread_local ThreadSubAllocator t_subAlloc;
        if (!t_subAlloc.m_isRegistered)
        {
            m_registeredSubAllocators.push_back(&t_subAlloc);
            t_subAlloc.m_isRegistered = true;
        }
        return t_subAlloc;
    }

    void CommandListAllocator::Init(const Descriptor& descriptor)
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
            commandAllocatorPoolDescriptor.m_dx12Device = descriptor.m_device->GetDX12Device();
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
                for (ThreadSubAllocator* subAlloc : m_registeredSubAllocators)
                {
                    Reset(*subAlloc, queueIdx);
                }
                m_commandListPools[queueIdx].Shutdown();
                m_commandAllocatorPools[queueIdx].Shutdown();
            }

            for (ThreadSubAllocator* subAlloc : m_registeredSubAllocators)
            {
                subAlloc->m_isRegistered = false;
            }
            m_registeredSubAllocators.clear();
            m_isInitialized = false;
        }
    }

    CommandList* CommandListAllocator::Allocate(RHI::HardwareQueueClass hardwareQueueClass)
    {
        ASSERT(m_isInitialized, "CommandListAllocator is not initialized!");
        const uint32_t hardwareQueue = static_cast<uint32_t>(hardwareQueueClass);

        ThreadSubAllocator& subAlloc = GetThreadSubAllocator();

        if (!subAlloc.m_activeCommandAllocators[hardwareQueue])
        {
            subAlloc.m_activeCommandAllocators[hardwareQueue] =
                m_commandAllocatorPools[hardwareQueue].Allocate();
        }

        CommandList* commandList = m_commandListPools[hardwareQueue].Allocate(
            subAlloc.m_activeCommandAllocators[hardwareQueue].get());
        subAlloc.m_activeLists.push_back(commandList);
        return commandList;
    }

    void CommandListAllocator::Reset(ThreadSubAllocator& subAlloc, uint32_t hardwareQueue)
    {
        for (size_t i = 0; i < subAlloc.m_activeLists.size(); )
        {
            if (static_cast<uint32_t>(subAlloc.m_activeLists[i]->GetHardwareQueueClass()) == hardwareQueue)
            {
                m_commandListPools[hardwareQueue].DeAllocate(subAlloc.m_activeLists[i].get());
                subAlloc.m_activeLists.erase(subAlloc.m_activeLists.begin() + i);
            }
            else
            {
                ++i;
            }
        }

        if (subAlloc.m_activeCommandAllocators[hardwareQueue])
        {
            m_commandAllocatorPools[hardwareQueue].DeAllocate(
                subAlloc.m_activeCommandAllocators[hardwareQueue].get());
            subAlloc.m_activeCommandAllocators[hardwareQueue].reset();
        }
    }

    void CommandListAllocator::Collect()
    {
        for (uint32_t queueIdx = 0; queueIdx < RHI::HardwareQueueClassCount; ++queueIdx)
        {
            for (ThreadSubAllocator* subAlloc : m_registeredSubAllocators)
            {
                Reset(*subAlloc, queueIdx);
            }
            m_commandListPools[queueIdx].Collect();
            m_commandAllocatorPools[queueIdx].Collect();
        }
    }
}