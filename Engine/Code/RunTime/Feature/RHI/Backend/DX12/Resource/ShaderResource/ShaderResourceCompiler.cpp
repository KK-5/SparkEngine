#include "ShaderResourceCompiler.h"

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
    RHI::ResultCode ShaderResourceCompiler::InitInternal(
        RHI::Device& device,
        const RHI::ShaderResourceCompilerDescriptor& desc)
    {
        return RHI::ResultCode::Success;
    }

    ResultCode ShaderResourceCompiler::CompilerInternal(eastl::span<RHI::ShaderResource*> shaderResources)
    {
        for (auto& srg : shaderResources)
        {
            const RHI::ShaderResourceLayout* layout = srg->GetLayout();
            ASSERT(layout, "[ShaderResourceCompiler] SRG has no layout.");

            ShaderResource& dx12Srg = static_cast<ShaderResource&>(*srg);

            // Allocate GPU resources on first compile
            if (!dx12Srg.m_viewsDescriptorTable.IsValid()
                && !dx12Srg.m_samplersDescriptorTable.IsValid()
                && !dx12Srg.m_constantMemoryView.IsValid())
            {
                AllocateShaderResource(*srg, *layout);
            }

            // Update all dirty SRGs (advance ring index + fill data)
            UpdateShaderResource(*srg);
        }

        return RHI::ResultCode::Success;
    }

    void ShaderResourceCompiler::AllocateShaderResource(
        RHI::ShaderResource& shaderResourceBase,
        const RHI::ShaderResourceLayout& layout)
    {
        ShaderResource& dx12Srg = static_cast<ShaderResource&>(shaderResourceBase);
        Device& device = static_cast<Device&>(GetDevice());
        ID3D12FactoryInterface* factory = Service<ID3D12FactoryInterface>::Get();
        DescriptorContext& descriptorCtx = factory->AcquireDescriptorContext(device);

        const uint32_t frameCountMax = device.GetDescriptor().m_frameCountMax;
        const uint32_t viewsSize = layout.GetBuffersSize() + layout.GetImagesSize();
        const uint32_t samplersSize = layout.GetSamplersSize();
        const uint32_t constantSize = AlignUp(layout.GetConstantDataSize(), Alignment::Constant);

        // -- Views descriptor table (ring-buffered) --
        if (viewsSize > 0)
        {
            const uint32_t viewsRingSize = viewsSize * frameCountMax;
            dx12Srg.m_viewsDescriptorTable = descriptorCtx.CreateDescriptorTable(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, viewsRingSize);
            ASSERT(dx12Srg.m_viewsDescriptorTable.IsValid(),
                "[ShaderResourceCompiler] Failed to allocate views descriptor table.");

            for (uint32_t i = 0; i < frameCountMax; ++i)
            {
                DescriptorTable frameTable(
                    dx12Srg.m_viewsDescriptorTable[viewsSize * i],
                    viewsSize);
                dx12Srg.m_compiledData[i].m_gpuViewsDescriptorHandle =
                    descriptorCtx.GetGpuNativeHandleForTable(frameTable);
            }
        }

        // -- Samplers descriptor table (ring-buffered) --
        if (samplersSize > 0)
        {
            const uint32_t samplersRingSize = samplersSize * frameCountMax;
            dx12Srg.m_samplersDescriptorTable = descriptorCtx.CreateDescriptorTable(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, samplersRingSize);
            ASSERT(dx12Srg.m_samplersDescriptorTable.IsValid(),
                "[ShaderResourceCompiler] Failed to allocate samplers descriptor table.");

            for (uint32_t i = 0; i < frameCountMax; ++i)
            {
                DescriptorTable frameTable(
                    dx12Srg.m_samplersDescriptorTable[samplersSize * i],
                    samplersSize);
                dx12Srg.m_compiledData[i].m_gpuSamplersDescriptorHandle =
                    descriptorCtx.GetGpuNativeHandleForTable(frameTable);
            }
        }

        // -- Constant buffer (ring-buffered) --
        if (constantSize > 0)
        {
            const size_t byteSize = AlignUp(constantSize, RHI::Alignment::Constant);
            const uint32_t constantRingSize = byteSize * frameCountMax;
            ConstantBufferContext& constantBufferCtx = factory->AcquireConstantBufferContext(device);
            dx12Srg.m_constantMemoryView =
                constantBufferCtx.CreateConstantBuffer(constantRingSize, RHI::Alignment::Constant);

            CpuVirtualAddress cpuAddress = dx12Srg.m_constantMemoryView.Map(RHI::HostMemoryAccess::Write);
            GpuVirtualAddress gpuAddress = dx12Srg.m_constantMemoryView.GetGpuAddress();

            for (uint32_t i = 0; i < frameCountMax; ++i)
            {
                dx12Srg.m_compiledData[i].m_gpuConstantAddress = gpuAddress + constantSize * i;
                dx12Srg.m_compiledData[i].m_cpuConstantAddress = cpuAddress + constantSize * i;
            }
        }
    }

    void ShaderResourceCompiler::UpdateShaderResource(RHI::ShaderResource& shaderResourceBase)
    {
        ShaderResource& dx12Srg = static_cast<ShaderResource&>(shaderResourceBase);
        const RHI::ShaderResourceLayout* layout = shaderResourceBase.GetLayout();
        ASSERT(layout, "[ShaderResourceCompiler] SRG has no layout.");

        const uint32_t frameCountMax = static_cast<Device&>(GetDevice()).GetDescriptor().m_frameCountMax;

        // Advance ring buffer index
        dx12Srg.m_compiledDataIndex = (dx12Srg.m_compiledDataIndex + 1) % frameCountMax;
        const uint32_t frameIndex = dx12Srg.m_compiledDataIndex;

        // -- Update views --
        uint32_t viewsSize = layout->GetBuffersSize() + layout->GetImagesSize();
        if (viewsSize > 0 && dx12Srg.m_viewsDescriptorTable.IsValid())
        {
            DescriptorTable frameViewsTable(
                dx12Srg.m_viewsDescriptorTable[frameIndex * viewsSize],
                viewsSize);
            UpdateViewsDescriptorTable(frameViewsTable, shaderResourceBase, *layout);
        }

        // -- Update samplers --
        uint32_t samplersSize = layout->GetSamplersSize();
        if (samplersSize > 0 && dx12Srg.m_samplersDescriptorTable.IsValid())
        {
            DescriptorTable frameSamplersTable(
                dx12Srg.m_samplersDescriptorTable[frameIndex * samplersSize],
                samplersSize);
            UpdateSamplersDescriptorTable(frameSamplersTable, shaderResourceBase, *layout);
        }

        // -- Update constants --
        if (layout->GetConstantDataSize() > 0)
        {
            const ShaderResourceCompiledData& compiledData = dx12Srg.m_compiledData[frameIndex];
            eastl::span<const uint8_t> constantData = shaderResourceBase.GetConstantData();
            memcpy(compiledData.m_cpuConstantAddress, constantData.data(), constantData.size());
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Descriptor table fill

    void ShaderResourceCompiler::UpdateViewsDescriptorTable(
        DescriptorTable descriptorTable,
        RHI::ShaderResource& shaderResourceBase,
        const RHI::ShaderResourceLayout& layout)
    {
        // Fill buffer descriptors
        RHI::ShaderInputIndex shaderInputIndex = 0;
        for (const RHI::ShaderInputBufferDescriptor& shaderInputBuffer : layout.GetShaderInputListForBuffers())
        {
            eastl::span<const ConstPtr<RHI::BufferView>> bufferViews =
                shaderResourceBase.GetBufferViewArray(shaderInputIndex);

            D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType =
                ConvertShaderInputBufferAccess(shaderInputBuffer.m_access);

            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
            switch (descriptorRangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                GetSRVsFromImageViews<RHI::BufferView, BufferView>(
                    bufferViews, D3D12_SRV_DIMENSION_BUFFER, descriptorHandles);
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                GetUAVsFromImageViews<RHI::BufferView, BufferView>(
                    bufferViews, D3D12_UAV_DIMENSION_BUFFER, descriptorHandles);
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                GetCBVsFromBufferViews(bufferViews, descriptorHandles);
                break;
            default:
                ASSERT(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                break;
            }

            UpdateDescriptorTableRangeForBuffer(descriptorTable, descriptorHandles, shaderInputIndex, layout);
            ++shaderInputIndex;
        }

        // Fill image descriptors
        shaderInputIndex = 0;
        for (const RHI::ShaderInputImageDescriptor& shaderInputImage : layout.GetShaderInputListForImages())
        {
            eastl::span<const ConstPtr<RHI::ImageView>> imageViews =
                shaderResourceBase.GetImageViewArray(shaderInputIndex);

            D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType =
                ConvertShaderInputImageAccess(shaderInputImage.m_access);

            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles;
            switch (descriptorRangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                GetSRVsFromImageViews<RHI::ImageView, ImageView>(
                    imageViews, ConvertSRVDimension(shaderInputImage.m_type), descriptorHandles);
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                GetUAVsFromImageViews<RHI::ImageView, ImageView>(
                    imageViews, ConvertUAVDimension(shaderInputImage.m_type), descriptorHandles);
                break;
            default:
                ASSERT(false, "Unhandled D3D12_DESCRIPTOR_RANGE_TYPE enumeration");
                break;
            }

            UpdateDescriptorTableRangeForImage(descriptorTable, descriptorHandles, shaderInputIndex, layout);
            ++shaderInputIndex;
        }
    }

    void ShaderResourceCompiler::UpdateSamplersDescriptorTable(
        DescriptorTable descriptorTable,
        RHI::ShaderResource& shaderResourceBase,
        const RHI::ShaderResourceLayout& layout)
    {
        ShaderResource& shaderResource = static_cast<ShaderResource&>(shaderResourceBase);
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        const DescriptorHandle nullHandle = descriptorCtx.GetNullHandleSampler();

        const size_t shaderInputSize = layout.GetShaderInputListForSamplers().size();
        for (size_t shaderInputIndex = 0; shaderInputIndex < shaderInputSize; ++shaderInputIndex)
        {
            eastl::span<const RHI::SamplerState> samplerStates =
                shaderResourceBase.GetSamplerArray(shaderInputIndex);
            eastl::fixed_vector<DescriptorHandle, SRGViewsFixedSize> descriptorHandles
                (static_cast<uint32_t>(samplerStates.size()), nullHandle);
            for(size_t i = 0; i < samplerStates.size(); ++i)
            {
                auto samplerState = samplerStates[i];
                Ptr<Sampler> sampler = nullptr;
                if (shaderResource.m_samplers.find(samplerState) == shaderResource.m_samplers.end())
                {
                    sampler = Service<ID3D12FactoryInterface>::Get()->CreateSampler();
                    sampler->Init(device, samplerState);
                    shaderResource.m_samplers.insert({samplerState, sampler});
                }
                else
                {
                    sampler = shaderResource.m_samplers[samplerState];
                }
                descriptorHandles[i] = sampler->GetDescriptorHandle();
            }
            // UpdateDescriptorTableRange(descriptorTable, samplers, shaderInputIndex, layout);
            UpdateDescriptorTableRangeForSampler(descriptorTable, descriptorHandles, shaderInputIndex, layout);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sub-table helpers

    DescriptorTable ShaderResourceCompiler::GetBufferTable(
        DescriptorTable descriptorTable,
        RHI::ShaderInputIndex bufferIndex,
        const RHI::ShaderResourceLayout& layout,
        uint32_t bufferOffset) const
    {
        const Interval interval = layout.GetGroupIntervalForBuffer(bufferIndex);
        const DescriptorHandle startHandle = descriptorTable[bufferOffset + interval.m_min];
        return DescriptorTable(startHandle, static_cast<uint32_t>(interval.m_max - interval.m_min));
    }

    DescriptorTable ShaderResourceCompiler::GetImageTable(
        DescriptorTable descriptorTable,
        RHI::ShaderInputIndex imageIndex,
        const RHI::ShaderResourceLayout& layout,
        uint32_t imageOffset) const
    {
        const Interval interval = layout.GetGroupIntervalForImage(imageIndex);
        const DescriptorHandle startHandle = descriptorTable[imageOffset + interval.m_min];
        return DescriptorTable(startHandle, static_cast<uint32_t>(interval.m_max - interval.m_min));
    }

    DescriptorTable ShaderResourceCompiler::GetSamplerTable(
        DescriptorTable descriptorTable,
        RHI::ShaderInputIndex samplerInputIndex,
        const RHI::ShaderResourceLayout& layout) const
    {
        const Interval interval = layout.GetGroupIntervalForSampler(samplerInputIndex);
        const DescriptorHandle startHandle = descriptorTable[interval.m_min];
        return DescriptorTable(startHandle, static_cast<uint32_t>(interval.m_max - interval.m_min));
    }

    //////////////////////////////////////////////////////////////////////////
    // Descriptor table range updates

    void ShaderResourceCompiler::UpdateDescriptorTableRangeForBuffer(
        DescriptorTable descriptorTable,
        const eastl::span<DescriptorHandle>& descriptors,
        RHI::ShaderInputIndex bufferInputIndex,
        const RHI::ShaderResourceLayout& layout)
    {
        const uint32_t bufferOffset = 0;
        const DescriptorTable destinationTable = GetBufferTable(descriptorTable, bufferInputIndex, layout, bufferOffset);
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorCtx.UpdateDescriptorTableRange(destinationTable, descriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void ShaderResourceCompiler::UpdateDescriptorTableRangeForImage(
        DescriptorTable descriptorTable,
        const eastl::span<DescriptorHandle>& descriptors,
        RHI::ShaderInputIndex imageInputIndex,
        const RHI::ShaderResourceLayout& layout)
    {
        const uint32_t imageOffset = layout.GetBuffersSize();
        const DescriptorTable destinationTable = GetImageTable(descriptorTable, imageInputIndex, layout, imageOffset);
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorCtx.UpdateDescriptorTableRange(destinationTable, descriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void ShaderResourceCompiler::UpdateDescriptorTableRangeForSampler(
        DescriptorTable descriptorTable,
        const eastl::span<DescriptorHandle>& descriptors,
        RHI::ShaderInputIndex samplerIndex,
        const RHI::ShaderResourceLayout& layout)
    {
        const uint32_t samplerOffset = 0;
        const DescriptorTable destinationTable = GetSamplerTable(descriptorTable, samplerIndex, layout);
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorCtx = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorCtx.UpdateDescriptorTableRange(destinationTable, descriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    void ShaderResourceCompiler::UpdateDescriptorTableRange(
        DescriptorTable descriptorTable,
        eastl::span<const RHI::SamplerState> samplerStates,
        RHI::ShaderInputIndex samplerIndex,
        const RHI::ShaderResourceLayout& layout)
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
            Ptr<Sampler> sampler = Service<ID3D12FactoryInterface>::Get()->CreateSampler();
            sampler->Init(device, samplerStates[i]);
            samplers[i] = sampler;
            sourceDescriptors[i] = samplers[i]->GetDescriptorHandle();
        }

        const DescriptorTable destinationTable = GetSamplerTable(descriptorTable, samplerIndex, layout);
        descriptorCtx.UpdateDescriptorTableRange(
            destinationTable, sourceDescriptors.data(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    //////////////////////////////////////////////////////////////////////////
    // Descriptor gather helpers

    template<typename T, typename U>
    void ShaderResourceCompiler::GetSRVsFromImageViews(
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
    void ShaderResourceCompiler::GetUAVsFromImageViews(
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

    void ShaderResourceCompiler::GetCBVsFromBufferViews(
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
