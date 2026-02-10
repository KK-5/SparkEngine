/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineLayout.h"

#include <EASTL/vector.h>
#include <Log/SpdLogSystem.h>

#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <Device/Device.h>
#include <Conversions.h>

namespace Spark::RHI::DX12
{
    void PipelineLayout::Shutdown()
    {
        m_d3d12Device = nullptr;
        m_hash = 0;
        DeviceObject::Shutdown();
    }

    void PipelineLayout::BuildRootCanstants(const PipelineLayoutDescriptor* desc, eastl::vector<D3D12_ROOT_PARAMETER>& parameters)
    {
        const RootConstantBinding& rootConstantBinding = desc->GetRootConstantBinding();

        m_hasRootConstants = (rootConstantBinding.m_constantCount > 0);

        if (m_hasRootConstants)
        {
            m_rootConstantsRootParameterIndex = RootParameterIndex(parameters.size());
            parameters.emplace_back();
            D3D12_ROOT_PARAMETER& parameter = parameters.back();

            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.Constants.Num32BitValues = rootConstantBinding.m_constantCount;
            parameter.Constants.ShaderRegister = rootConstantBinding.m_constantRegister;
            parameter.Constants.RegisterSpace = rootConstantBinding.m_constantRegisterSpace;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
    }

    void PipelineLayout::BuildShaderResourceConstants(
        const PipelineLayoutDescriptor* desc,
        const eastl::vector<uint8_t>& sortedIndex,
        eastl::vector<D3D12_ROOT_PARAMETER>& parameters)
    {
        for (const uint32_t groupLayoutIndex : sortedIndex)
        {
            const RHI::ShaderResourceLayout& groupLayout = *desc->GetShaderResourceLayout(groupLayoutIndex);
            const RHI::ShaderResourceBindingInfo& groupBindInfo = desc->GetShaderResourceBindingInfo(groupLayoutIndex);

            if (groupLayout.GetConstantDataSize())
            {
                const auto& bindingInfo = desc->GetShaderResourceBindingInfo(groupLayoutIndex);
                const uint32_t registerSpace = bindingInfo.m_constantDataBindingInfo.m_spaceId;
                const uint32_t cbvRegister = groupBindInfo.m_constantDataBindingInfo.m_registerId;

                D3D12_ROOT_PARAMETER parameter;
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

                parameter.ShaderVisibility = ConvertShaderStageMask(groupBindInfo.m_constantDataBindingInfo.m_shaderStageMask);

                parameter.Descriptor.ShaderRegister = cbvRegister;
                parameter.Descriptor.RegisterSpace = registerSpace;

                m_indexToRootParameterBindingTable[groupLayoutIndex].m_constantBuffer = RootParameterIndex(parameters.size());
                parameters.push_back(parameter);
            }
        }
    }

    void PipelineLayout::BuildShaderResourceBuffersAndImages(
        const PipelineLayoutDescriptor* desc,
        const eastl::vector<uint8_t>& sortedIndex,
        eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[])
    {
        for (const uint32_t groupLayoutIndex : sortedIndex)
        {
            const RHI::ShaderResourceLayout& groupLayout = *desc->GetShaderResourceLayout(groupLayoutIndex);
            const RHI::ShaderResourceBindingInfo& groupBindInfo = desc->GetShaderResourceBindingInfo(groupLayoutIndex);
            const ShaderResourceVisibility& groupVisibility = desc->GetShaderResourceVisibility(groupLayoutIndex);

            if (groupLayout.GetBuffersSize() || groupLayout.GetImagesSize())
            {
                for (const RHI::ShaderInputBufferDescriptor& shaderInputBuffer : groupLayout.GetShaderInputListForBuffers())
                {
                    auto findIt = groupBindInfo.m_resourcesRegisterMap.find(shaderInputBuffer.m_name);
                    ASSERT(findIt != groupBindInfo.m_resourcesRegisterMap.end(), "Could not find register for shader input %s", shaderInputBuffer.m_name.GetCStr());
                    const RHI::ResourceBindingInfo& bindingInfo = findIt->second;
                    D3D12_DESCRIPTOR_RANGE descriptorRange;
                    descriptorRange.RegisterSpace = bindingInfo.m_spaceId;
                    descriptorRange.NumDescriptors = shaderInputBuffer.m_count;
                    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    descriptorRange.BaseShaderRegister = bindingInfo.m_registerId;

                    switch (shaderInputBuffer.m_access)
                    {
                    case RHI::ShaderInputBufferAccess::Constant:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                        break;

                    case RHI::ShaderInputBufferAccess::Read:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;

                    case RHI::ShaderInputBufferAccess::ReadWrite:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    }

                    descriptorRanges[groupLayoutIndex].push_back(descriptorRange);
                }

                for (const RHI::ShaderInputImageDescriptor& shaderInputImage : groupLayout.GetShaderInputListForImages())
                {
                    auto findIt = groupBindInfo.m_resourcesRegisterMap.find(shaderInputImage.m_name);
                    ASSERT(findIt != groupBindInfo.m_resourcesRegisterMap.end(), "Could not find register for shader input %s", shaderInputImage.m_name.GetCStr());

                    const RHI::ResourceBindingInfo& bindingInfo = findIt->second;
                    D3D12_DESCRIPTOR_RANGE descriptorRange;
                    descriptorRange.RegisterSpace = bindingInfo.m_spaceId;
                    descriptorRange.NumDescriptors = shaderInputImage.m_count;
                    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    descriptorRange.BaseShaderRegister = bindingInfo.m_registerId;

                    switch (shaderInputImage.m_access)
                    {
                    case RHI::ShaderInputImageAccess::Read:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;

                    case RHI::ShaderInputImageAccess::ReadWrite:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    }

                    descriptorRanges[groupLayoutIndex].push_back(descriptorRange);
                }

                D3D12_ROOT_PARAMETER parameter;
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameter.ShaderVisibility = ConvertShaderStageMask(groupVisibility.m_descriptorTableShaderStageMask);
                parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRanges[groupLayoutIndex].size());
                parameter.DescriptorTable.pDescriptorRanges = descriptorRanges[groupLayoutIndex].data();

                m_indexToRootParameterBindingTable[groupLayoutIndex].m_resourceTable = RootParameterIndex(parameters.size());
                parameters.push_back(parameter);
            }
        }
    }

    void PipelineLayout::Init(Device& device, const RHI::PipelineLayoutDescriptor& descriptor)
    {
        m_hash = descriptor.GetHash();
        m_d3d12Device = device.GetDevice();
        m_layoutDescriptor = &descriptor;

        const uint32_t groupLayoutCount = static_cast<uint32_t>(descriptor.GetShaderResourceLayoutCount());
        ASSERT(groupLayoutCount <= RHI::Limits::Pipeline::ShaderResourceCountMax, "Exceeded ShaderResourceGroupLayout count limit.");

        eastl::vector<D3D12_ROOT_PARAMETER> parameters;
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[RHI::Limits::Pipeline::ShaderResourceCountMax];
        eastl::vector<D3D12_DESCRIPTOR_RANGE> unboundedArraydescriptorRanges[RHI::Limits::Pipeline::ShaderResourceCountMax];
        eastl::vector<D3D12_DESCRIPTOR_RANGE> samplerDescriptorRanges[RHI::Limits::Pipeline::ShaderResourceCountMax];
        eastl::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;

        const PipelineLayoutDescriptor* dx12Descriptor = static_cast<const PipelineLayoutDescriptor*>(&descriptor);

        /**
         * If the pipeline layout uses an inline constant binding, that becomes the very first
         * parameter in the root signature.
         */
        BuildRootCanstants(dx12Descriptor, parameters);

        // Initialise mapping tables
        m_slotToIndexTable.fill(static_cast<uint8_t>(RHI::Limits::Pipeline::ShaderResourceCountMax));
        m_indexToRootParameterBindingTable.resize(groupLayoutCount);
        m_indexToSlotTable.resize(groupLayoutCount);

        for (uint32_t groupLayoutIndex = 0; groupLayoutIndex < groupLayoutCount; ++groupLayoutIndex)
        {
            const RHI::ShaderResourceLayout& groupLayout = *descriptor.GetShaderResourceLayout(groupLayoutIndex);

            const uint32_t srgLayoutSlot = groupLayout.GetBindingSlot();
            m_slotToIndexTable[srgLayoutSlot] = static_cast<uint8_t>(groupLayoutIndex);
            m_indexToSlotTable[groupLayoutIndex] = static_cast<uint8_t>(srgLayoutSlot);
        }

        // Construct a list of indexes sorted by frequency.
        // nVidia recommends to construct the root signature with higher execution frequency first https://developer.nvidia.com/dx12-dos-and-donts#roots
        eastl::vector<uint8_t> indexesSortedByFrequency(groupLayoutCount);
        uint32_t usedSlotIndex = 0;
        for (uint32_t slot = 0; slot < m_slotToIndexTable.size(); ++slot)
        {
            const uint8_t groupLayoutIndex = m_slotToIndexTable[slot];
            if (groupLayoutIndex != RHI::Limits::Pipeline::ShaderResourceCountMax)
            {
                indexesSortedByFrequency[usedSlotIndex] = groupLayoutIndex;
                ++usedSlotIndex;
            }
        }
        ASSERT(usedSlotIndex == groupLayoutCount, "Unexpected number of used slots");

        // Next, front-load by frequency the SRG Constants. Each SRG with Constants adds a constant buffer entry as root parameters of the root signature.
        BuildShaderResourceConstants(dx12Descriptor, indexesSortedByFrequency, parameters);
        
        DeviceObject::Init(device);
    }

    size_t PipelineLayout::GetRootParameterBindingCount() const
    {
        return m_indexToRootParameterBindingTable.size();
    }

    RootParameterBinding PipelineLayout::GetRootParameterBindingByIndex(size_t index) const
    {
        return m_indexToRootParameterBindingTable[index];
    }

    RootParameterIndex PipelineLayout::GetRootConstantsRootParameterIndex() const
    {
        return m_rootConstantsRootParameterIndex;
    }

    size_t PipelineLayout::GetSlotByIndex(size_t index) const
    {
        return m_indexToSlotTable[index];
    }

    size_t PipelineLayout::GetIndexBySlot(size_t slot) const
    {
        return m_slotToIndexTable[slot];
    }

    ID3D12RootSignature* PipelineLayout::Get() const
    {
        return m_signature.get();
    }

    size_t PipelineLayout::GetHash() const
    {
        return m_hash;
    }

    const RHI::PipelineLayoutDescriptor& PipelineLayout::GetPipelineLayoutDescriptor() const
    {
        return *m_layoutDescriptor;
    }
}