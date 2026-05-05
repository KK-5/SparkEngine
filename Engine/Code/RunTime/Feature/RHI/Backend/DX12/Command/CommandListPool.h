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

#pragma once

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <Object/ObjectPool.h>
#include <Memory/PoolAllocator.h>

#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <DX12.h>

#include "CommandList.h"

namespace Spark::RHI::DX12
{
    class Device;

    class CommandAllocatorFactory final : public IObjectFactory<ID3D12CommandAllocator>
    {
    public:
        struct Descriptor
        {
            HardwareQueueClass m_hardwareQueueClass = HardwareQueueClass::Graphics;

            Ptr<ID3D12DeviceX> m_dx12Device;
        };

        void Init(const Descriptor& descriptor);

        ID3D12CommandAllocator* CreateObject();

        void DestoryObject(ID3D12CommandAllocator* allocator, bool isPoolShutdown);

        void ResetObject(ID3D12CommandAllocator* allocator);

        bool IsRecycleObject(ID3D12CommandAllocator* allocator);

    private:
        Descriptor m_descriptor;
    };

    struct CommandAllocatorPoolTraits : public ObjectPoolTraits
    {
        using ObjectType = ID3D12CommandAllocator;
        using ObjectFactoryType = CommandAllocatorFactory;
        using MutexType = std::mutex;
    };

    /**
     * In DirectX 12, Command Allocators are linear memory allocators for command list.
     * The recommended practice by nVidia / AMD is to keep N * T of them around, where
     * N is the number of buffered frames, T is the number of threads. The CommandAllocatorPool
     * handles allocating and retiring command allocators in a round-robin fashion.
     * The class used by CommandListAllocator and CommandListSubAllocator.
     */
    using CommandAllocatorPool = ObjectPool<CommandAllocatorPoolTraits>;


    // CommandListPool
    class CommandListFactory final : public IObjectFactory<CommandList>
    {
    public:
        struct Descriptor
        {
            Device* m_device = nullptr;

            HardwareQueueClass m_hardwareQueueClass = HardwareQueueClass::Graphics;
        };

        void Init(const Descriptor& descriptor);

        CommandList* CreateObject(ID3D12CommandAllocator* commandAllocator);

        void ResetObject(CommandList* commandList, ID3D12CommandAllocator* commandAllocator);

        void DestoryObject(CommandList* commandList, bool isPoolShutdown);
        
        bool IsRecycleObject(CommandList* commandList);

    private:
        Descriptor m_descriptor;
        PoolAllocator m_allocator;
    };

    struct CommandListPoolTraits : public ObjectPoolTraits
    {
        using ObjectType = CommandList;
        using ObjectFactoryType = CommandListFactory;
        using MutexType = std::recursive_mutex;
    };

    /**
     * CommandListPool is a simple round-robin allocator of command lists. It takes
     * a lock with each allocation, and requires the CommandAllocator instance associate
     * with the new command list instance. CommandAllocators are not thread-safe and are
     * pooled separately from command list. The CommandListAllocator class combines
     * the CommandListPool and CommandAllocatorPools together with a per-thread sub-allocator
     * to facilitate more ideal allocation of command lists.
     */
    using CommandListPool = ObjectPool<CommandListPoolTraits>;

    class CommandListAllocator final
    {
    public:
        CommandListAllocator() = default;

        struct Descriptor
        {
            // The device used for creating the command lists.
            Device* m_device = nullptr;

            // The maximum number of frames to keep buffered on the CPU timeline.
            uint32_t m_frameCountMax = RHI::Limits::Device::FrameCountMax;
        };

        void Init(const Descriptor& descriptor);

        void Shutdown();

        CommandList* Allocate(RHI::HardwareQueueClass hardwareQueueClass);

        // Call this once per frame to retire the current frame and reclaim
        // elements from completed frames
        void Collect();

    private:
        // Per-thread active allocator and command lists.
        // ID3D12CommandAllocator is not thread-safe, so each thread must own its allocator.
        struct ThreadSubAllocator
        {
            eastl::array<Ptr<ID3D12CommandAllocator>, RHI::HardwareQueueClassCount> m_activeCommandAllocators;
            eastl::vector<Ptr<CommandList>> m_activeLists;
            bool m_isRegistered = false;
        };

        ThreadSubAllocator& GetThreadSubAllocator();
        void Reset(ThreadSubAllocator& subAlloc, uint32_t hardwareQueue);

        eastl::array<CommandListPool, RHI::HardwareQueueClassCount> m_commandListPools;
        eastl::array<CommandAllocatorPool, RHI::HardwareQueueClassCount> m_commandAllocatorPools;

        // Pointers to thread-local sub-allocators so Collect() can reset them.
        // Collect and Allocate are never concurrent (frame boundary), so registration needs no lock.
        eastl::vector<ThreadSubAllocator*> m_registeredSubAllocators;

        bool m_isInitialized = false;
    };
}