/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove BufferPoolResolver and GetResolver, barrier management moved to RHI::CommandList.
 */
#pragma once

#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <RHI/Device/DeviceObjectFactory.h>
#include <ReleaseQueue.h>
#include <VirtualBlockAllocation.h>

namespace Spark::RHI::DX12
{
    class Device;
    class Buffer;

    class BufferPool : public RHI::BufferPool
    {
    public:
        virtual ~BufferPool() = default;

    private:
        friend class DeviceObjectFactory<BufferPool>;

        BufferPool() = default;

        Device& GetDevice() const;

        //////////////////////////////////////////////////////////////////////////
        // FrameEventBus::Handler
        void OnFrameEnd() override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // RHI::BufferPool
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::BufferPoolDescriptor& descriptor) override;
        void ShutdownInternal() override;
        RHI::ResultCode InitBufferInternal(
            RHI::Buffer& buffer, const RHI::BufferDescriptor& rhiDescriptor) override;
        void ShutdownResourceInternal(RHI::Resource& resource) override;
        RHI::ResultCode OrphanBufferInternal(RHI::Buffer& buffer) override;
        RHI::ResultCode MapBufferInternal(const RHI::BufferMapRequest& mapRequest, RHI::BufferMapResponse& response) override;
        void UnmapBufferInternal(RHI::Buffer& buffer) override;
        //////////////////////////////////////////////////////////////////////////

        RHI::ResultCode InitBaseBuffer(const RHI::BufferPoolDescriptor& descriptor);
        // InitBufferInternal's two ways: a resource of the buffer's own, or a part of the base buffer.
        RHI::ResultCode InitUniqueBuffer(Buffer& buffer, const RHI::BufferDescriptor& descriptor);
        RHI::ResultCode InitSubAllocatedBuffer(Buffer& buffer, const RHI::BufferDescriptor& descriptor);

        Ptr<D3D12MA::Allocator> m_allocator;
        D3D12MAReleaseQueue m_releaseQueue;

        // Only in a pool with a budget: the one native buffer every buffer of the pool is a
        // part of, the record of which parts are taken, and the parts buffers gave back.
        Ptr<D3D12MA::Allocation>           m_baseBuffer;
        Ptr<D3D12MA::VirtualBlock>         m_virtualBlock;
        VirtualBlockAllocationReleaseQueue m_virtualBlockReleaseQueue;
    };
}