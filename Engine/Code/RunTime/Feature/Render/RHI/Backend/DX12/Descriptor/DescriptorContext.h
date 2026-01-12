/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Use static variable s_descriptorHeapLimits instead PlatformLimitsDescriptor
 *  -- 
 */

#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/array.h>

#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Device/DeviceObject.h>

#include <DX12.h>
#include "DescriptorPool.h"

namespace Spark::RHI::DX12
{
    class Device;
    class Buffer;
    class Image;

    class DescriptorContext final : public RHI::DeviceObject
    {
    public:
        DescriptorContext() = default;
        ~DescriptorContext() noexcept = default;

        void Init(Device& device);

        void CreateConstantBufferView(
            const Buffer& buffer,
            const RHI::BufferViewDescriptor& bufferViewDescriptor,
            DescriptorHandle& constantBufferView,
            DescriptorHandle& staticView);

        void CreateShaderResourceView(
            const Buffer& buffer,
            const RHI::BufferViewDescriptor& bufferViewDescriptor,
            DescriptorHandle& shaderResourceView,
            DescriptorHandle& staticView);

        void CreateUnorderedAccessView(
            const Buffer& buffer,
            const RHI::BufferViewDescriptor& bufferViewDescriptor,
            DescriptorHandle& unorderedAccessView,
            DescriptorHandle& unorderedAccessViewClear,
            DescriptorHandle& staticView);

        void CreateShaderResourceView(
            const Image& image,
            const RHI::ImageViewDescriptor& imageViewDescriptor,
            DescriptorHandle& shaderResourceView,
            DescriptorHandle& staticView);

        void CreateUnorderedAccessView(
            const Image& image,
            const RHI::ImageViewDescriptor& imageViewDescriptor,
            DescriptorHandle& unorderedAccessView,
            DescriptorHandle& unorderedAccessViewClear,
            DescriptorHandle& staticView);

        void CreateRenderTargetView(
            const Image& image,
            const RHI::ImageViewDescriptor& imageViewDescriptor,
            DescriptorHandle& renderTargetView);

        void CreateDepthStencilView(
            const Image& image,
            const RHI::ImageViewDescriptor& imageViewDescriptor,
            DescriptorHandle& depthStencilView,
            DescriptorHandle& depthStencilReadView);

        void CreateSampler(
            const RHI::SamplerState& samplerState,
            DescriptorHandle& samplerHandle);

        void ReleaseDescriptor(DescriptorHandle descriptorHandle);

        void ReleaseStaticDescriptor(DescriptorHandle handle);

        //! Creates a GPU-visible descriptor table.
        //! @param descriptorHeapType The descriptor heap to allocate from.
        //! @param descriptorCount The number of descriptors to allocate.
        DescriptorTable CreateDescriptorTable(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType, uint32_t descriptorCount);

        //! Releases a GPU-visible descriptor table.
        //! @param descriptorHeapType The descriptor heap to allocate from.
        void ReleaseDescriptorTable(DescriptorTable descriptorTable);
        
        //! Performs a gather of disjoint CPU-side descriptors and copies to a contiguous GPU-side descriptor table.
        //! @param gpuDestinationTable The destination descriptor table that the descriptors will be uploaded to.
        //!     This must be the table specifically for a given range of descriptors, so if
        //!     the user created a table with multiple ranges, they are required to partition
        //!     that table and call this method multiple times with each range partition.
        //!
        //! @param cpuSourceDescriptors The CPU descriptors being gathered and copied to the destination table. 
        //!     The number of elements must match the size of the destination table.
        //! @param heapType The type of heap being updated.
        void UpdateDescriptorTableRange(
            DescriptorTable gpuDestinationTable,
            const DescriptorHandle* cpuSourceDescriptors,
            D3D12_DESCRIPTOR_HEAP_TYPE heapType);
        
        DescriptorHandle GetNullHandleSRV(D3D12_SRV_DIMENSION dimension) const;
        DescriptorHandle GetNullHandleUAV(D3D12_UAV_DIMENSION dimension) const;
        DescriptorHandle GetNullHandleCBV() const;
        DescriptorHandle GetNullHandleSampler() const;

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandle(DescriptorHandle handle) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandle(DescriptorHandle handle) const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuNativeHandleForTable(DescriptorTable descTable) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuNativeHandleForTable(DescriptorTable descTable) const;

        //! Retrieve a descriptor handle to the start of the static region of the shader-visible CBV_SRV_UAV heap
        D3D12_GPU_DESCRIPTOR_HANDLE GetBindlessGpuNativeHandle() const;

        void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList) const;

        void Collect();

    private:
        // 定义每种D3D12_DESCRIPTOR_HEAP_TYPE可以创建的两种D3D12_DESCRIPTOR_HEAP_FLAGS描述符数量
        static const eastl::unordered_map<uint32_t, eastl::array<uint32_t, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE + 1>> s_descriptorHeapLimits;
        static const uint32_t NumHeapFlags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE + 1; // 2
        static const float m_staticDescriptorRatio;

        // Offset from the shader-visible descriptor heap start to the first static descriptor handle
        uint32_t m_staticDescriptorOffset = 0;

        void CopyDescriptor(DescriptorHandle dest, DescriptorHandle src);

        // Accepts a descriptor allocated from the CPU visible heap and creates a copy in the shader-
        // visible heap in the static region
        DescriptorHandle AllocateStaticDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle);

        void CreateNullDescriptors();
        void CreateNullDescriptorsSRV();
        void CreateNullDescriptorsUAV();
        void CreateNullDescriptorsCBV();
        void CreateNullDescriptorsSampler();

        template<D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags>
        DescriptorPool& GetPool();

        template<D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags>
        const DescriptorPool& GetPool() const;

        DescriptorPool& GetPool(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags);
        const DescriptorPool& GetPool(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags) const;

        //! Allocates a Descriptor table which describes a contiguous range of descriptor handles
        template<D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags>
        DescriptorTable AllocateTable(uint32_t count);
        DescriptorTable AllocateTable(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, uint32_t count);

        //! Allocates a single descriptor handle
        template<D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags>
        DescriptorHandle AllocateHandle();
        DescriptorHandle AllocateHandle(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags);

        bool IsShaderVisibleCbvSrvUavHeap(uint32_t type, uint32_t flag) const;

        DescriptorPool m_CBVSRVUAVHeapFlagNone;
        DescriptorPool m_CBVSRVUAVHeapFlagShaderVisible;
        DescriptorPool m_SamplerHeapFlagNone;
        DescriptorPool m_SamplerHeapFlagShaderVisible;
        DescriptorPool m_RTVHeapFlagNone;
        DescriptorPool m_DSVHeapFlagNone;
        // The static pool is a region of the shader-visible descriptor heap used to store descriptors that persist for the
        // lifetime of the resource view they reference
        DescriptorPool m_staticPool;

        // This table binds the entire range of CBV_SRV_UAV descriptor handles in the shader visible heap
        DescriptorTable m_staticTable;

        // Get the device from the RHI::Device that inherited from DeviceObject, so just hold the row pointer
        ID3D12DeviceX* m_D3D12Device;

        eastl::unordered_map<D3D12_SRV_DIMENSION, DescriptorHandle> m_nullDescriptorsSRV;
        eastl::unordered_map<D3D12_UAV_DIMENSION, DescriptorHandle> m_nullDescriptorsUAV;
        DescriptorHandle m_nullDescriptorCBV;
        DescriptorHandle m_nullSamplerDescriptor;
    };

    template<> DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>()
    {
        return m_CBVSRVUAVHeapFlagNone;
    }

    template<> const DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>() const
    {
        return GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
    }

    template<> DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>()
    {
        return m_CBVSRVUAVHeapFlagShaderVisible;
    }

    template<> const DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>() const
    {
        return GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>();
    }

    template<> DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>()
    {
        return m_SamplerHeapFlagNone;
    }

    template<> const DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>() const
    {
        return GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
    }

    template<> DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>()
    {
        return m_SamplerHeapFlagShaderVisible;
    }

    template<> const DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>() const
    {
        return GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>();
    }

    template<> DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>()
    {
        return m_RTVHeapFlagNone;
    }

    template<> const DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>() const
    {
        return GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
    }

    template<> DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>()
    {
        return m_DSVHeapFlagNone;
    }

    template<> const DescriptorPool& 
        DescriptorContext::GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>() const
    {
        return GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
    }

    template<D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags>
    DescriptorHandle DescriptorContext::AllocateHandle()
    {
        GetPool<type, flags>().AllocateHandle();
    }

    template<D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags>
    DescriptorTable DescriptorContext::AllocateTable(uint32_t count)
    {
        GetPool<type, flags>().AllocateTable(count);
    }
}