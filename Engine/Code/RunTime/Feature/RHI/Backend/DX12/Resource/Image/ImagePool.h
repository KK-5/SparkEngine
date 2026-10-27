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
    class Image;
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

        // InitImageInternal's two ways: a reserved resource, or a committed one of the image's own.
        RHI::ResultCode InitReservedImage(Image& image, const RHI::ImageDescriptor& descriptor,
                                          D3D12_RESOURCE_DESC resourceDesc, const D3D12_CLEAR_VALUE* clearValue);
        RHI::ResultCode InitUniqueImage(Image& image, const D3D12_RESOURCE_DESC& resourceDesc,
                                        const D3D12_CLEAR_VALUE* clearValue);

        // A pool of the device's allocator. Its allocations wait out the GPU here rather than on
        // the device: D3D12MA releases a pool only after all of them.
        Ptr<D3D12MA::Pool> m_pool;
        D3D12MAReleaseQueue m_releaseQueue;
    };
}