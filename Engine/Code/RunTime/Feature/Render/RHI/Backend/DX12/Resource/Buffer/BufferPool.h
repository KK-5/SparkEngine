/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <ReleaseQueue.h>

namespace Spark::RHI::DX12
{
    class BufferPoolResolver;
    
    class BufferPool : public RHI::BufferPool
    {
        using Base = RHI::BufferPool;
    public:
        virtual ~BufferPool() = default;

    private:
        BufferPool() = default;

        Device& GetDevice() const;

        //////////////////////////////////////////////////////////////////////////
        // FrameSchedulerEventBus::Handler
        void OnFrameEnd() override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // RHI::DeviceBufferPool
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::BufferPoolDescriptor& descriptor) override;
        void ShutdownInternal() override;
        RHI::ResultCode InitBufferInternal(
            RHI::Buffer& buffer, const RHI::BufferDescriptor& rhiDescriptor) override;
        void ShutdownResourceInternal(RHI::Resource& resource) override;
        RHI::ResultCode OrphanBufferInternal(RHI::Buffer& buffer) override;
        RHI::ResultCode MapBufferInternal(const RHI::BufferMapRequest& mapRequest, RHI::BufferMapResponse& response) override;
        void UnmapBufferInternal(RHI::Buffer& buffer) override;
        RHI::ResultCode StreamBufferInternal(const RHI::BufferStreamRequest& request) override;
        //////////////////////////////////////////////////////////////////////////

        BufferPoolResolver* GetResolver();

        Ptr<D3D12MA::Allocator> m_allocator;
        D3D12MAReleaseQueue m_releaseQueue;
    };
}