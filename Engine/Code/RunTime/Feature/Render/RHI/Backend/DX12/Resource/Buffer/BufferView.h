/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <RHI/Resource/Buffer/BufferView.h>

#include <Descriptor/Descriptor.h>

namespace Spark::RHI::DX12
{
    class Buffer;

    class BufferView final : public RHI::BufferView
    {
        using Base = RHI::BufferView;
    public:
        const Buffer& GetBuffer() const;

        DescriptorHandle GetReadDescriptor() const;
        DescriptorHandle GetReadWriteDescriptor() const;
        DescriptorHandle GetClearDescriptor() const;
        DescriptorHandle GetConstantDescriptor() const;

        GpuVirtualAddress GetGpuAddress() const;
        ID3D12Resource* GetMemory() const;

        //////////////////////////////////////////////////////////////////////////
        // RHI::BufferView
        uint32_t GetBindlessReadIndex() const override;
        uint32_t GetBindlessReadWriteIndex() const override;
        uint64_t GetDeviceAddress() const override;
        //////////////////////////////////////////////////////////////////////////

    private:
        BufferView() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::BufferView
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::Resource& resourceBase) override;
        // RHI::ResultCode InvalidateInternal() override;
        void ShutdownInternal() override;
        //////////////////////////////////////////////////////////////////////////

        DescriptorHandle m_readDescriptor;
        DescriptorHandle m_readWriteDescriptor;
        DescriptorHandle m_clearDescriptor;
        DescriptorHandle m_constantDescriptor;
        GpuVirtualAddress m_gpuAddress = 0;

        // The following indicies are offsets to the static descriptor associated with this
        // resource view in the static region of the shader-visible descriptor heap
        DescriptorHandle m_staticReadDescriptor;
        DescriptorHandle m_staticReadWriteDescriptor;
        DescriptorHandle m_staticConstantDescriptor;

        ID3D12Resource* m_memory = nullptr;
    };
}