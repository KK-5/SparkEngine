/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/unordered_map.h>

#include <DX12.h>
#include <ReleaseQueue.h>
#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <3rdParty/D3D12MA/D3D12MemAlloc.h>

namespace Spark::RHI::DX12
{
    class Device;
    class ImagePoolResolver;

    class ImagePool final : public RHI::ImagePool
    {
        using Base = RHI::ImagePool;
    public:
        Device& GetDevice() const;
    
    private:
        ImagePool() = default;

        friend class DeviceObjectFactory<ImagePool>;

        ImagePoolResolver* GetResolver();

        //////////////////////////////////////////////////////////////////////////
        // FrameEventBus::Handler
        void OnFrameEnd() override;
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // RHI::ImagePool
        RHI::ResultCode InitInternal(RHI::Device&, const RHI::ImagePoolDescriptor&) override;
        RHI::ResultCode InitImageInternal(const RHI::ImageInitRequest& request) override;
        void ShutdownResourceInternal(RHI::Resource& resourceBase) override;
        void ShutdownInternal() override;
        //////////////////////////////////////////////////////////////////////////

        Ptr<D3D12MA::Allocator> m_d3dmaAllocator;
        D3D12MAReleaseQueue m_releaseQueue;
    };
}