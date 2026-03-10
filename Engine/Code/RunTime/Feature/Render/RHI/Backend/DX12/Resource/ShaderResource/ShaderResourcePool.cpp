/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderResourcePool.h"

#include <Math/Bit.h>
#include <Log/SpdLogSystem.h>

#include <ID3D12Factory.h>
#include <Conversions.h>
#include <Device/Device.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/Constant/ConstantBufferContext.h>
#include <Resource/Buffer/BufferView.h>
#include <Resource/Image/ImageView.h>
#include <Resource/Sampler/Sampler.h>
#include "ShaderResource.h"

namespace Spark::RHI::DX12
{
    RHI::ResultCode ShaderResourcePool::InitInternal(RHI::Device& deviceBase, const RHI::ShaderResourcePoolDescriptor& descriptor)
    {
        Device& device = static_cast<Device&>(deviceBase);

        const RHI::ShaderResourceLayout& layout = *descriptor.m_layout;
        const uint32_t frameCountMax = device.GetDescriptor().m_frameCountMax;
        m_constantBufferSize = AlignUp(layout.GetConstantDataSize(), Alignment::Constant);
        m_constantBufferRingSize = m_constantBufferSize * frameCountMax;
        m_viewsDescriptorTableSize = layout.GetBuffersSize() + layout.GetImagesSize();
        m_viewsDescriptorTableRingSize = m_viewsDescriptorTableSize * frameCountMax;
        m_samplersDescriptorTableSize = layout.GetSamplersSize();
        m_samplersDescriptorTableRingSize = m_samplersDescriptorTableSize * frameCountMax;

        // Buffers occupy the first region of the table, then images.
        m_descriptorTableBufferOffset = 0;
        m_descriptorTableImageOffset = layout.GetBuffersSize();

        m_unboundedArrayCount = layout.GetBufferUnboundedArraySize() + layout.GetImageUnboundedArraySize();

        return RHI::ResultCode::Success;
    }

    void ShaderResourcePool::ShutdownInternal()
    {
        Base::ShutdownInternal();
    }

    RHI::ResultCode ShaderResourcePool::InitShaderResourceInternal(RHI::ShaderResource& shaderResourceBase)
    {
        ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
        Device& device = static_cast<Device&>(GetDevice());

        ID3D12FactoryInterface* factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(factory, "ID3D12Factory does not initialized");

        ConstantBufferContext& constantBufferCtx = factory->AcquireConstantBufferContext();
        DescriptorContext& descriptorCtx = factory->AcquireDescriptorContext(device);

        const uint32_t copyCount = device.GetDescriptor().m_frameCountMax;
        if (m_constantBufferSize)
        {
            shaderResource.m_constantMemoryView = constantBufferCtx.CreateConstantBuffer(m_constantBufferRingSize, RHI::Alignment::Constant);
            CpuVirtualAddress cpuAddress = shaderResource.m_constantMemoryView.Map(RHI::HostMemoryAccess::Write);
            GpuVirtualAddress gpuAddress = shaderResource.m_constantMemoryView.GetGpuAddress();

            for (uint32_t i = 0; i < copyCount; ++i)
            {
                ShaderResourceCompiledData& compiledData = shaderResource.m_compiledData[i];
                compiledData.m_gpuConstantAddress = gpuAddress + m_constantBufferSize * i;
                compiledData.m_cpuConstantAddress = cpuAddress + m_constantBufferSize * i;
            }
        }

        if (m_samplersDescriptorTableSize)
        {
            shaderResource.m_samplersDescriptorTable = descriptorCtx.CreateDescriptorTable(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_samplersDescriptorTableRingSize);
            if (!shaderResource.m_samplersDescriptorTable.IsValid())
            {
                LOG_ERROR("[ShaderResourcePool] Descriptor context failed to allocate sampler descriptor table.");
                return RHI::ResultCode::OutOfMemory;
            }

            for (uint32_t i = 0; i < copyCount; ++i)
            {
                const DescriptorHandle descriptorHandle = shaderResource.m_samplersDescriptorTable.GetOffset() + m_samplersDescriptorTableSize * i;

                ShaderResourceCompiledData& compiledData = shaderResource.m_compiledData[i];
                compiledData.m_gpuSamplersDescriptorHandle = descriptorCtx.GetGpuNativeHandleForTable(DescriptorTable(descriptorHandle, static_cast<uint16_t>(m_samplersDescriptorTableSize)));
            }
        }

        return RHI::ResultCode::Success;
    }

    void ShaderResourcePool::ShutdownResourceInternal(RHI::Resource& resourceBase)
    {
        ShaderResource& shaderResource = static_cast<ShaderResource&>(resourceBase);

        if (m_constantBufferSize)
        {
            ConstantBufferContext& constantBufferCtx = Service<ID3D12FactoryInterface>::Get()->AcquireConstantBufferContext();
            shaderResource.m_constantMemoryView.Unmap(RHI::HostMemoryAccess::Write);
            constantBufferCtx.CollectConstantBuffer(shaderResource.m_constantMemoryView);
        }

        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        if (m_viewsDescriptorTableSize)
        {
            if (shaderResource.m_viewsDescriptorTable.IsValid())
            {
                descriptorCtx.ReleaseDescriptorTable(shaderResource.m_viewsDescriptorTable);
            }
        }

        if (m_samplersDescriptorTableSize)
        {
            if (shaderResource.m_viewsDescriptorTable.IsValid())
            {
                descriptorCtx.ReleaseDescriptorTable(shaderResource.m_samplersDescriptorTable);
            }
        }

        // [TODO] unbounded view 

        shaderResource.m_compiledDataIndex = 0;
        shaderResource.m_compiledData.fill(ShaderResourceCompiledData());

        Base::ShutdownResourceInternal(resourceBase);
    }

    void ShaderResourcePool::OnFrameEnd()
    {
        Base::OnFrameEnd();
    }

    ResultCode ShaderResourcePool::CompileBufferViewAndImageView(RHI::ShaderResource& shaderResourceBase)
    {
        if (m_viewsDescriptorTableSize)
        {
            ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
            Device& device = static_cast<Device&>(GetDevice());
            DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
            shaderResource.m_compiledDataIndex = (shaderResource.m_compiledDataIndex + 1) % GetDevice().GetDescriptor().m_frameCountMax;
            //Lazy initialization for cbv/srv/uav Descriptor Tables
            if (!shaderResource.m_viewsDescriptorTable.IsValid())
            {
                shaderResource.m_viewsDescriptorTable = descriptorCtx.CreateDescriptorTable(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_viewsDescriptorTableRingSize);

                if (!shaderResource.m_viewsDescriptorTable.IsValid())
                {
                    ASSERT(false, "Descriptor heap ran out of memory.");
                    return RHI::ResultCode::OutOfMemory;
                }

                CacheGpuHandlesForViews(shaderResource);
            }

            const DescriptorTable descriptorTable(
                shaderResource.m_viewsDescriptorTable.GetOffset() + shaderResource.m_compiledDataIndex * m_viewsDescriptorTableSize,
                static_cast<uint16_t>(m_viewsDescriptorTableSize));

            UpdateViewsDescriptorTable(descriptorTable, shaderResource);
        }
        return RHI::ResultCode::Success;
    }

    ResultCode ShaderResourcePool::CompileShaderReourceBufferInternal(RHI::ShaderResource& shaderResourceBase)
    {
        return CompileBufferViewAndImageView(shaderResourceBase);
    }

    ResultCode ShaderResourcePool::CompileShaderReourceImageInternal(RHI::ShaderResource& shaderResourceBase)
    {
        return CompileBufferViewAndImageView(shaderResourceBase);
    }

    ResultCode ShaderResourcePool::CompileShaderReourceConstantDataInternal(RHI::ShaderResource& shaderResourceBase)
    {
        if (m_constantBufferSize)
        {
            ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
            Device& device = static_cast<Device&>(GetDevice());
            DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
            shaderResource.m_compiledDataIndex = (shaderResource.m_compiledDataIndex + 1) % GetDevice().GetDescriptor().m_frameCountMax;

            memcpy(shaderResource.GetCompiledData().m_cpuConstantAddress, shaderResource.GetConstantData().data(), shaderResource.GetConstantData().size());
        }
        return RHI::ResultCode::Success;
    }

    ResultCode ShaderResourcePool::CompileShaderReourceSamplerInternal(RHI::ShaderResource& shaderResourceBase)
    {
        if (m_samplersDescriptorTableSize)
        {
            ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
            Device& device = static_cast<Device&>(GetDevice());
            DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
            shaderResource.m_compiledDataIndex = (shaderResource.m_compiledDataIndex + 1) % GetDevice().GetDescriptor().m_frameCountMax;

            // Sampler的DesciptorHandle已经在初始化时创建
            const DescriptorTable descriptorTable(
                shaderResource.m_samplersDescriptorTable.GetOffset() + shaderResource.m_compiledDataIndex * m_samplersDescriptorTableSize,
                static_cast<uint16_t>(m_samplersDescriptorTableSize));

            UpdateSamplersDescriptorTable(descriptorTable, shaderResource);
        }
        return RHI::ResultCode::Success;
    }

    void ShaderResourcePool::CacheGpuHandlesForViews(ShaderResource& shaderResource)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        for (uint32_t i = 0; i < GetDevice().GetDescriptor().m_frameCountMax; ++i)
        {
            const DescriptorHandle descriptorHandle = shaderResource.m_viewsDescriptorTable.GetOffset() + m_viewsDescriptorTableSize * i;

            ShaderResourceCompiledData& compiledData = shaderResource.m_compiledData[i];
            compiledData.m_gpuViewsDescriptorHandle = descriptorCtx.GetGpuNativeHandleForTable(
                DescriptorTable(descriptorHandle, static_cast<uint16_t>(m_viewsDescriptorTableSize)));
        }
    }

    void ShaderResourcePool::UpdateViewsDescriptorTable(DescriptorTable descriptorTable, RHI::ShaderResource& shaderResourceBase)
    {
        ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
        const RHI::ShaderResourceLayout& layout = *shaderResource.GetLayout();

        RHI::ShaderInputIndex shaderInputIndex = 0;
        for (const RHI::ShaderInputBufferDescriptor& shaderInputBuffer : layout.GetShaderInputListForBuffers())
        {
            eastl::span<const ConstPtr<RHI::BufferView>> bufferViews = shaderResource.GetBufferViewArray(shaderInputIndex);
            D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType = ConvertShaderInputBufferAccess(shaderInputBuffer.m_access);
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
            switch (descriptorRangeType)
            {
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                {
                    GetSRVsFromImageViews<RHI::BufferView, BufferView>(
                        bufferViews, D3D12_SRV_DIMENSION_BUFFER, descriptorHandles);
                    break;
                }
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                {
                    GetUAVsFromImageViews<RHI::BufferView, BufferView>(
                        bufferViews, D3D12_UAV_DIMENSION_BUFFER, descriptorHandles);
                    break;
                }
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                {
                    GetCBVsFromBufferViews(bufferViews, descriptorHandles);
                    break;
                }
                default:
                    ASSERT(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                    break;
            }

            UpdateDescriptorTableRangeForBuffer(descriptorTable, descriptorHandles, shaderInputIndex);
            ++shaderInputIndex;
        }

        shaderInputIndex = 0;
        for (const RHI::ShaderInputImageDescriptor& shaderInputImage : layout.GetShaderInputListForImages())
        {
            eastl::span<const ConstPtr<RHI::ImageView>> imageViews = shaderResource.GetImageViewArray(shaderInputIndex);
            D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType = ConvertShaderInputImageAccess(shaderInputImage.m_access);
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
            switch (descriptorRangeType)
            {
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                {
                    GetSRVsFromImageViews<RHI::ImageView, ImageView>(
                        imageViews, ConvertSRVDimension(shaderInputImage.m_type), descriptorHandles);
                    break;
                }
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                {
                    GetUAVsFromImageViews<RHI::ImageView, ImageView>(
                        imageViews, ConvertUAVDimension(shaderInputImage.m_type), descriptorHandles);
                    break;
                }
                default:
                ASSERT(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                break;
            }

            UpdateDescriptorTableRangeForImage(descriptorTable, descriptorHandles, shaderInputIndex);
            ++shaderInputIndex;
        }
    }

    void ShaderResourcePool::UpdateSamplersDescriptorTable(DescriptorTable descriptorTable, RHI::ShaderResource& shaderResourceBase)
    {
        ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
        const RHI::ShaderResourceLayout& layout = *shaderResource.GetLayout();
        const size_t shaderInputSize = layout.GetShaderInputListForSamplers().size();
        for (size_t shaderInputIndex = 0; shaderInputIndex < shaderInputSize; ++shaderInputIndex)
        {
            eastl::span<const RHI::SamplerState> samplers = shaderResource.GetSamplerArray(shaderInputIndex);
            UpdateDescriptorTableRange(descriptorTable, samplers, shaderInputIndex);
        }
    }

    void ShaderResourcePool::UpdateUnboundedArrayDescriptorTables(RHI::ShaderResource& shaderResourceBase)
    {

    }

    void ShaderResourcePool::UpdateUnboundedBuffersDescTable(
        DescriptorTable descriptorTable,
        RHI::ShaderInputIndex shaderInputIndex,
        RHI::ShaderInputBufferAccess bufferAccess)
    {

    }

    //! Update all the image views for the unbounded array
    void ShaderResourcePool::UpdateUnboundedImagesDescTable(
        DescriptorTable descriptorTable,
        RHI::ShaderInputIndex shaderInputIndex,
        RHI::ShaderInputImageAccess imageAccess,
        RHI::ShaderInputImageType imageType)
    {

    }

    DescriptorTable ShaderResourcePool::GetBufferTable(DescriptorTable descriptorTable, RHI::ShaderInputIndex bufferIndex) const
    {
        const Interval interval = GetLayout()->GetGroupIntervalForBuffer(bufferIndex);
        const DescriptorHandle startHandle = descriptorTable[m_descriptorTableBufferOffset + interval.m_min];
        return DescriptorTable(startHandle, static_cast<uint32_t>(interval.m_max - interval.m_min));
    }
    DescriptorTable ShaderResourcePool::GetImageTable(DescriptorTable descriptorTable, RHI::ShaderInputIndex imageIndex) const
    {
        const Interval interval = GetLayout()->GetGroupIntervalForImage(imageIndex);
        const DescriptorHandle startHandle = descriptorTable[m_descriptorTableImageOffset + interval.m_min];
        return DescriptorTable(startHandle, static_cast<uint32_t>(interval.m_max - interval.m_min));
    }

    DescriptorTable ShaderResourcePool::GetSamplerTable(DescriptorTable descriptorTable, RHI::ShaderInputIndex samplerInputIndex) const
    {
        const Interval interval = GetLayout()->GetGroupIntervalForSampler(samplerInputIndex);
        const DescriptorHandle startHandle = descriptorTable[interval.m_min];
        return DescriptorTable(startHandle, static_cast<uint32_t>(interval.m_max - interval.m_min));
    }

    void ShaderResourcePool::UpdateDescriptorTableRangeForBuffer(DescriptorTable descriptorTable, const eastl::span<DescriptorHandle>& descriptors, RHI::ShaderInputIndex bufferInputIndex)
    {
        const DescriptorTable destinationTable = GetBufferTable(descriptorTable, bufferInputIndex);
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorCtx.UpdateDescriptorTableRange(destinationTable, descriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void ShaderResourcePool::UpdateDescriptorTableRangeForImage(DescriptorTable descriptorTable, const eastl::span<DescriptorHandle>& descriptors, RHI::ShaderInputIndex imageInputIndex)
    {
        const DescriptorTable destinationTable = GetBufferTable(descriptorTable, imageInputIndex);
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorCtx.UpdateDescriptorTableRange(destinationTable, descriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void ShaderResourcePool::UpdateDescriptorTableRange(DescriptorTable descriptorTable, eastl::span<const RHI::SamplerState> samplerStates, RHI::ShaderInputIndex samplerIndex)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        const DescriptorHandle nullHandle = descriptorCtx.GetNullHandleSampler();
        eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize> sourceDescriptors(
            static_cast<uint32_t>(samplerStates.size()), nullHandle);
        eastl::fixed_vector<ConstPtr<Sampler>, SRGViewsFixedSize> samplers(
            static_cast<uint32_t>(samplerStates.size()), ConstPtr<Sampler>());
        
        for (size_t i = 0; i < samplerStates.size(); ++i)
        {
            samplers[i] = Service<ID3D12FactoryInterface>::Get()->CreateSampler(samplerStates[i]);
            sourceDescriptors[i] = samplers[i]->GetDescriptorHandle();
        }

        const DescriptorTable destinationTable = GetSamplerTable(descriptorTable, samplerIndex);
        descriptorCtx.UpdateDescriptorTableRange(
            destinationTable, sourceDescriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }


    template<typename T, typename U>
    void ShaderResourcePool::GetSRVsFromImageViews(
        const eastl::span<const ConstPtr<T>>& imageViews,
        D3D12_SRV_DIMENSION dimension,
        eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        result.resize(imageViews.size(), descriptorCtx.GetNullHandleSRV(dimension));

        for (size_t i = 0; i < result.size(); ++i)
        {
            if (imageViews[i])
            {
                result[i] = static_cast<const U*>(imageViews[i].get())->GetReadDescriptor();
            }
        }
    }

    template<typename T, typename U>
    void ShaderResourcePool::GetUAVsFromImageViews(
        const eastl::span<const ConstPtr<T>>& imageViews,
        D3D12_UAV_DIMENSION dimension,
        eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        result.resize(imageViews.size(), descriptorCtx.GetNullHandleUAV(dimension));
        for (size_t i = 0; i < result.size(); ++i)
        {
            if (imageViews[i])
            {
                result[i] = static_cast<const U*>(imageViews[i].get())->GetReadWriteDescriptor();
            }
        }
    }

    void ShaderResourcePool::GetCBVsFromBufferViews(
        const eastl::span<const ConstPtr<RHI::BufferView>>& bufferViews,
        eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize>& result)
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        result.resize(bufferViews.size(), descriptorCtx.GetNullHandleCBV());

        for (size_t i = 0; i < bufferViews.size(); ++i)
        {
            if (bufferViews[i])
            {
                result[i] = static_cast<const BufferView*>(bufferViews[i].get())->GetConstantDescriptor();
            }
        }
    }
}