/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "DescriptorContext.h"

#include <Log/SpdLogSystem.h>

//#include <RHI/Device/Device.h>
#include <Device/Device.h>
#include <Resource/Buffer/Buffer.h>
#include <Conversions.h>

namespace Spark::RHI::DX12
{
    const eastl::unordered_map<uint32_t, eastl::array<uint32_t, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE + 1>> DescriptorContext::s_descriptorHeapLimits =
    {
        {static_cast<uint32_t>(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), { 100000, 1000000 } },
        {static_cast<uint32_t>(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),     { 2048,   2048    } },
        {static_cast<uint32_t>(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),         { 2048,   0       } },
        {static_cast<uint32_t>(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),         { 2048,   0       } }
    };

    const float DescriptorContext::m_staticDescriptorRatio = 0.5f;

    bool DescriptorContext::IsShaderVisibleCbvSrvUavHeap(uint32_t type, uint32_t flag) const
    {
        return type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && flag == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    }

    void DescriptorContext::CreateNullDescriptors()
    {
        CreateNullDescriptorsSRV();
        CreateNullDescriptorsUAV();
        CreateNullDescriptorsCBV();
        CreateNullDescriptorsSampler();
    }

    void DescriptorContext::CreateNullDescriptorsSRV()
    {
        const eastl::array<D3D12_SRV_DIMENSION, 10> validSRVDimensions = 
        {   
            D3D12_SRV_DIMENSION_BUFFER,
            D3D12_SRV_DIMENSION_TEXTURE1D,
            D3D12_SRV_DIMENSION_TEXTURE1DARRAY,
            D3D12_SRV_DIMENSION_TEXTURE2D,
            D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
            D3D12_SRV_DIMENSION_TEXTURE2DMS,
            D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY,
            D3D12_SRV_DIMENSION_TEXTURE3D,
            D3D12_SRV_DIMENSION_TEXTURECUBE,
            D3D12_SRV_DIMENSION_TEXTURECUBEARRAY ,
        };

        for (D3D12_SRV_DIMENSION dimension : validSRVDimensions)
        {
            DescriptorHandle srvDescriptorHandle = AllocateHandle<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();

            D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
            desc.Format = DXGI_FORMAT_R32_UINT;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.ViewDimension = dimension;
            m_D3D12Device->CreateShaderResourceView(nullptr, &desc, GetCpuNativeHandle(srvDescriptorHandle));
            m_nullDescriptorsSRV[dimension] = srvDescriptorHandle;
        }
    }

    void DescriptorContext::CreateNullDescriptorsUAV()
    {
        const eastl::array<D3D12_UAV_DIMENSION, 6> UAVDimensions = 
        {
                D3D12_UAV_DIMENSION_BUFFER,
                D3D12_UAV_DIMENSION_TEXTURE1D,
                D3D12_UAV_DIMENSION_TEXTURE1DARRAY,
                D3D12_UAV_DIMENSION_TEXTURE2D,
                D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                D3D12_UAV_DIMENSION_TEXTURE3D };

            for (D3D12_UAV_DIMENSION dimension : UAVDimensions)
            {
                DescriptorHandle uavDescriptorHandle = AllocateHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

                D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
                desc.Format = DXGI_FORMAT_R32_UINT;
                desc.ViewDimension = dimension;
                m_D3D12Device->CreateUnorderedAccessView(nullptr, nullptr, &desc, GetCpuNativeHandle(uavDescriptorHandle));
                m_nullDescriptorsUAV[dimension] = uavDescriptorHandle;
            }
    }

    void DescriptorContext::CreateNullDescriptorsCBV()
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferDesc = {};
        DescriptorHandle cbvDescriptorHandle = AllocateHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        m_D3D12Device->CreateConstantBufferView(&constantBufferDesc, GetCpuNativeHandle(cbvDescriptorHandle));
        m_nullDescriptorCBV = cbvDescriptorHandle;
    }

    void DescriptorContext::CreateNullDescriptorsSampler()
    {
        m_nullSamplerDescriptor = AllocateHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        m_D3D12Device->CreateSampler(&samplerDesc, GetCpuNativeHandle(m_nullSamplerDescriptor));
    }

    void DescriptorContext::Init(Device& device)
    {
        DeviceObject::Init(device);
        m_D3D12Device = device.GetDevice();

        auto GetDesciptorCount = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flag)
        {
            auto desciptorCountArray = s_descriptorHeapLimits.at(static_cast<uint32_t>(type));
            return desciptorCountArray[static_cast<uint32_t>(flag)];
        };

        uint32_t desciptorCount = GetDesciptorCount(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        m_CBVSRVUAVHeapFlagNone.Init(
            device.GetDevice(), 
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            desciptorCount,
            desciptorCount
        );

        desciptorCount = GetDesciptorCount(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        uint32_t staticDesciptorCount = desciptorCount * m_staticDescriptorRatio;
        uint32_t dynamicDesciptorCount = desciptorCount - staticDesciptorCount;
        m_staticDescriptorOffset = dynamicDesciptorCount;
        m_CBVSRVUAVHeapFlagShaderVisible.Init(
            device.GetDevice(), 
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            desciptorCount,
            dynamicDesciptorCount
        );

        m_staticPool.InitPooledRange(device.GetDevice(), m_CBVSRVUAVHeapFlagShaderVisible.GetNativeHeap(), m_staticDescriptorOffset, staticDesciptorCount);

        desciptorCount = GetDesciptorCount(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        m_SamplerHeapFlagNone.Init(
            device.GetDevice(),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            desciptorCount,
            desciptorCount
        );

        desciptorCount = GetDesciptorCount(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        m_SamplerHeapFlagShaderVisible.Init(
            device.GetDevice(),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            desciptorCount,
            desciptorCount
        );

        desciptorCount = GetDesciptorCount(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        m_RTVHeapFlagNone.Init(
            device.GetDevice(),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            desciptorCount,
            desciptorCount
        );

        desciptorCount = GetDesciptorCount(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
        m_DSVHeapFlagNone.Init(
            device.GetDevice(),
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            desciptorCount,
            desciptorCount
        );
        
        CreateNullDescriptors();
    }

    void DescriptorContext::CreateConstantBufferView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        DescriptorHandle& constantBufferView,
        DescriptorHandle& staticView)
    {
        if (constantBufferView.IsNull())
        {
            constantBufferView = AllocateHandle<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
        }
        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetCpuNativeHandle(constantBufferView);

        D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
        ConvertBufferView(buffer, bufferViewDescriptor, viewDesc);
        m_D3D12Device->CreateConstantBufferView(&viewDesc, descriptorHandle);
        staticView = AllocateStaticDescriptor(descriptorHandle);
    }

    void DescriptorContext::CreateShaderResourceView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        DescriptorHandle& shaderResourceView,
        DescriptorHandle& staticView)
    {
        if (shaderResourceView.IsNull())
        {
            shaderResourceView = AllocateHandle<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
            if (shaderResourceView.IsNull())
            {
                ASSERT(false, "Descriptor heap ran out of memory for descriptor handles.");
                return;
            }
        }
        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = GetCpuNativeHandle(shaderResourceView);

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
        ConvertBufferView(buffer, bufferViewDescriptor, viewDesc);

        bool isRayTracingAccelerationStructure = CheckBitsAll(buffer.GetDescriptor().m_bindFlags, RHI::BufferBindFlags::RayTracingAccelerationStructure);
        ID3D12Resource* resource = isRayTracingAccelerationStructure ? nullptr : buffer.GetMemoryView().GetMemory();
        m_D3D12Device->CreateShaderResourceView(resource, &viewDesc, descriptorHandle);

        staticView = m_staticPool.AllocateHandle();
        ASSERT(!staticView.IsNull(), "Failed to allocate static descriptor from shader-visible CBV_SRV_UAV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC staticViewDesc;
        RHI::BufferViewDescriptor rawDesc = RHI::BufferViewDescriptor::CreateRaw(
            bufferViewDescriptor.m_elementOffset * bufferViewDescriptor.m_elementSize,
            bufferViewDescriptor.m_elementCount * bufferViewDescriptor.m_elementSize);
        ConvertBufferView(buffer, rawDesc, staticViewDesc);
        m_D3D12Device->CreateShaderResourceView(resource, &staticViewDesc, m_staticPool.GetCpuNativeHandle(staticView));
    }

    void DescriptorContext::CreateUnorderedAccessView(
        const Buffer& buffer,
        const RHI::BufferViewDescriptor& bufferViewDescriptor,
        DescriptorHandle& unorderedAccessView,
        DescriptorHandle& unorderedAccessViewClear,
        DescriptorHandle& staticView)
    {
        if (unorderedAccessView.IsNull())
        {
            unorderedAccessView = AllocateHandle<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
            if (unorderedAccessView.IsNull())
            {
                ASSERT(false, "Descriptor heap ran out of memory for descriptor handles.");
                return;
            }
        }
        D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessDescriptor = GetCpuNativeHandle(unorderedAccessView);

        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc;
        ConvertBufferView(buffer, bufferViewDescriptor, viewDesc);
        m_D3D12Device->CreateUnorderedAccessView(buffer.GetMemoryView().GetMemory(), nullptr, &viewDesc, unorderedAccessDescriptor);

        // Copy the UAV descriptor into the GPU-visible version for clearing.
        if (unorderedAccessViewClear.IsNull())
        {
            // [TODO] 
            unorderedAccessViewClear = AllocateHandle<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>();

            if (unorderedAccessViewClear.IsNull())
            {
                ASSERT(false, "Descriptor heap ran out of memory for descriptor handles.");
                return;
            }
        }
        CopyDescriptor(unorderedAccessViewClear, unorderedAccessView);

        staticView = m_staticPool.AllocateHandle();
        ASSERT(!staticView.IsNull(), "Failed to allocate static descriptor from shader-visible CBV_SRV_UAV heap");
        D3D12_UNORDERED_ACCESS_VIEW_DESC staticViewDesc;
        RHI::BufferViewDescriptor rawDesc = RHI::BufferViewDescriptor::CreateRaw(
            bufferViewDescriptor.m_elementOffset * bufferViewDescriptor.m_elementSize,
            bufferViewDescriptor.m_elementCount * bufferViewDescriptor.m_elementSize);
        ConvertBufferView(buffer, rawDesc, staticViewDesc);
        m_D3D12Device->CreateUnorderedAccessView(
            buffer.GetMemoryView().GetMemory(), nullptr, &staticViewDesc, m_staticPool.GetCpuNativeHandle(staticView));
    }

    void DescriptorContext::CreateSampler(
        const RHI::SamplerState& samplerState,
        DescriptorHandle& samplerHandle)
    {
        if (samplerHandle.IsNull())
        {
            samplerHandle = AllocateHandle<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_NONE>();
            if (samplerHandle.IsNull())
            {
                ASSERT(false, "Descriptor heap ran out of memory for descriptor handles.");
                return;
            }
        }

        D3D12_SAMPLER_DESC samplerDesc;
        ConvertSamplerState(samplerState, samplerDesc);
        m_D3D12Device->CreateSampler(&samplerDesc, GetCpuNativeHandle(samplerHandle));
    }

    DescriptorHandle DescriptorContext::AllocateStaticDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        DescriptorHandle staticHandle = m_staticPool.AllocateHandle();
        ASSERT(!staticHandle.IsNull(), "Failed to allocate static descriptor from shader-visible CBV_SRV_UAV heap");

        m_D3D12Device->CopyDescriptorsSimple(
            1, m_staticPool.GetCpuNativeHandle(staticHandle), handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return staticHandle;
    }

    void DescriptorContext::CopyDescriptor(DescriptorHandle dest, DescriptorHandle src)
    {
        ASSERT(dest.m_type == src.m_type, "Cannot copy descriptors from different heaps");
        ASSERT(!src.IsShaderVisible(), "The source descriptor cannot be shader visible.");
        m_D3D12Device->CopyDescriptorsSimple(1, GetCpuNativeHandle(dest), GetCpuNativeHandle(src), dest.m_type);
    }

    void DescriptorContext::ReleaseDescriptor(DescriptorHandle descriptorHandle)
    {
        if (!descriptorHandle.IsNull())
        {
            //GetPool(descriptorHandle.m_type, descriptorHandle.m_flags).ReleaseHandle(descriptorHandle);
            FindDescriptorHandlePool(descriptorHandle.m_type).ReleaseHandle(descriptorHandle);
        }
    }

    void DescriptorContext::ReleaseStaticDescriptor(DescriptorHandle handle)
    {
        if (!handle.IsNull())
        {
            m_staticPool.ReleaseHandle(handle);
        }
    }

    DescriptorTable DescriptorContext::CreateDescriptorTable(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType, uint32_t descriptorCount)
    {
        return AllocateTable(descriptorHeapType, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, descriptorCount);
    }

    void DescriptorContext::ReleaseDescriptorTable(DescriptorTable descriptorTable)
    {
        //GetPool(table.GetType(), table.GetFlags()).ReleaseTable(table);
        FindDescriptorTablePool(descriptorTable.GetType()).ReleaseTable(descriptorTable);
    }

    DescriptorPool<DescriptorHandlePool>& DescriptorContext::FindDescriptorHandlePool(D3D12_DESCRIPTOR_HEAP_TYPE type)
    {
        // ASSERT(!handle.IsNull(), "Trying to get pool with invalid handle");
        // ASSERT(!handle.IsShaderVisible(), "DescriptorHandlePool is only used for descriptor handles that are not visible to shaders.");
        switch (type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            return m_CBVSRVUAVHeapFlagNone;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return m_SamplerHeapFlagNone;
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            return m_RTVHeapFlagNone;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            return m_DSVHeapFlagNone;
        default:
            ASSERT(false, "Unknow Desciptor Heap!");
        }
    }

    const DescriptorPool<DescriptorHandlePool>& DescriptorContext::FindDescriptorHandlePool(D3D12_DESCRIPTOR_HEAP_TYPE type) const
    {
        return FindDescriptorHandlePool(type);
    }

    DescriptorPool<DescriptorTablePool>& DescriptorContext::FindDescriptorTablePool(D3D12_DESCRIPTOR_HEAP_TYPE type)
    {
        // ASSERT(!table.IsNull(), "Trying to get pool with invalid table");
        // ASSERT(table.GetOffset().IsShaderVisible(), "DescriptorTablePool is only used for descriptor handles that are visible to shaders.");
        switch (type)
        {
            case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
                return m_CBVSRVUAVHeapFlagShaderVisible;
            case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
                return m_SamplerHeapFlagShaderVisible;
            default:
                ASSERT(false, "Unknow Desciptor Heap!"); 
        }
    }

    const DescriptorPool<DescriptorTablePool>& DescriptorContext::FindDescriptorTablePool(D3D12_DESCRIPTOR_HEAP_TYPE type) const
    {
        return FindDescriptorTablePool(type);
    }


    DescriptorHandle DescriptorContext::AllocateHandle(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
    {
        if (CheckBitsAll(flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
        {
            LOG_ERROR("[DescriptorPool] Trying to allocate DescriptorHandle from a GPU visible DescriptorPool. Recommanded use AllocateTable instead.");
            return DescriptorHandle();
        }
        return FindDescriptorHandlePool(type).AllocateHandle();
    }

    DescriptorTable DescriptorContext::AllocateTable(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, uint32_t count)
    {
        if (!CheckBitsAll(flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
        {
            LOG_ERROR("[DescriptorPool] Trying to allocate DescriptorTable from a non GPU visible DescriptorPool. Recommanded use AllocateHandle instead.");
            return DescriptorTable();
        }

        return FindDescriptorTablePool(type).AllocateTable(count);
    }

}