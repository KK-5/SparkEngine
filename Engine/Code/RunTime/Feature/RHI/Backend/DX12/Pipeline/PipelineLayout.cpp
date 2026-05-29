/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- Remove the PipelineLayoutCache. ID3D12Factory will manager the PipelineLayout object.
 */

#include "PipelineLayout.h"

#include <EASTL/vector.h>
#include <Log/SpdLogSystem.h>

#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <Device/Device.h>
#include <Conversions.h>
#include <ID3D12Factory.h>

namespace Spark::RHI::DX12
{
    void PipelineLayout::Shutdown()
    {
        if (!IsInitialized())
        {
            return;
        }
        auto ID3D12Factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(ID3D12Factory, "ID3D12Factory is null!");
        ID3D12Factory->QueueForRelease(static_cast<Device&>(GetDevice()), eastl::move(m_signature));

        m_signature = nullptr;
        m_d3d12Device = nullptr;
        m_layoutDescriptor = nullptr;
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

    void PipelineLayout::BuildShaderResourceSamplers(
        const PipelineLayoutDescriptor* desc,
        const eastl::vector<uint8_t>& sortedIndex,
        eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[]
    )
    {
        for (const uint32_t groupLayoutIndex : sortedIndex)
        {
            const RHI::ShaderResourceLayout& groupLayout = *desc->GetShaderResourceLayout(groupLayoutIndex);
            const RHI::ShaderResourceBindingInfo& groupBindInfo = desc->GetShaderResourceBindingInfo(groupLayoutIndex);
            const ShaderResourceVisibility& groupVisibility = desc->GetShaderResourceVisibility(groupLayoutIndex);

            if (groupLayout.GetSamplersSize())
            {
                for (const RHI::ShaderInputSamplerDescriptor& shaderInputSampler : groupLayout.GetShaderInputListForSamplers())
                {
                    auto findIt = groupBindInfo.m_resourcesRegisterMap.find(shaderInputSampler.m_name);
                    ASSERT(findIt != groupBindInfo.m_resourcesRegisterMap.end(), "Could not find register for shader input %s", shaderInputSampler.m_name.GetCStr());

                    const RHI::ResourceBindingInfo& bindingInfo = findIt->second;
                    D3D12_DESCRIPTOR_RANGE descriptorRange;
                    descriptorRange.RegisterSpace = bindingInfo.m_spaceId;
                    descriptorRange.NumDescriptors = shaderInputSampler.m_count;
                    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    descriptorRange.BaseShaderRegister = bindingInfo.m_registerId;
                    descriptorRanges[groupLayoutIndex].push_back(descriptorRange);
                }

                D3D12_ROOT_PARAMETER parameter;
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameter.ShaderVisibility = ConvertShaderStageMask(groupVisibility.m_descriptorTableShaderStageMask);
                parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRanges[groupLayoutIndex].size());
                parameter.DescriptorTable.pDescriptorRanges = descriptorRanges[groupLayoutIndex].data();

                m_indexToRootParameterBindingTable[groupLayoutIndex].m_samplerTable = RootParameterIndex(parameters.size());
                parameters.push_back(parameter);
            }
        }
    }

    void PipelineLayout::BuildStaticSamplers(
        const PipelineLayoutDescriptor* desc,
        const eastl::vector<uint8_t>& sortedIndex,
        eastl::vector<D3D12_ROOT_PARAMETER>& parameters, 
        eastl::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers
    )
    {
        for (const uint32_t groupLayoutIndex : sortedIndex)
        {
            const RHI::ShaderResourceLayout& groupLayout = *desc->GetShaderResourceLayout(groupLayoutIndex);
            const RHI::ShaderResourceBindingInfo& groupBindInfo = desc->GetShaderResourceBindingInfo(groupLayoutIndex);

            for (const RHI::ShaderInputStaticSamplerDescriptor& samplerInput : groupLayout.GetStaticSamplers())
            {
                auto findRegisterIt = groupBindInfo.m_resourcesRegisterMap.find(samplerInput.m_name);
                ASSERT(findRegisterIt != groupBindInfo.m_resourcesRegisterMap.end(), "Could not find register for shader input %s", samplerInput.m_name.GetCStr());
                
                const RHI::ResourceBindingInfo& bindingInfo = findRegisterIt->second;
                D3D12_STATIC_SAMPLER_DESC desc;
                ConvertStaticSampler(
                    samplerInput.m_samplerState, bindingInfo.m_registerId, bindingInfo.m_spaceId,
                    ConvertShaderStageMask(bindingInfo.m_shaderStageMask), desc);

                staticSamplers.push_back(desc);
            }
        }
    }

    ////////////////////////////////////////////////////////////////
    /// New path
    void PipelineLayout::BuildSpaceGroupConstants(
            const PipelineLayoutDescriptor* desc,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters
    )
    {
        uint32_t groupCount = static_cast<uint32_t>(desc->GetSpaceGroupCount());
        for (uint32_t index = 0; index < groupCount; ++index)
        {
            const RHI::ShaderInputGroup& spaceGroup = desc->GetSpaceGroup(index);

            // One root CBV per unique register — constants sharing the same register
            // belong to the same cbuffer and must not produce duplicate root parameters.
            eastl::fixed_vector<uint32_t, SpaceCBVBinding::MaxCount> registers;

            for (auto handle : spaceGroup.m_shaderInputs)
            {
                if (handle.m_type != ShaderInputType::Constant)
                {
                    continue;
                }
                const RHI::ShaderInputConstantDescriptor& constantInput = desc->GetConstantDescriptor(handle.m_index);

                bool hasRegister = false;
                for (uint32_t reg : registers)
                {
                    if (reg == constantInput.m_registerId)
                    {
                        hasRegister = true;
                        break;
                    }
                }
                if (hasRegister)
                {
                    continue;
                }
                registers.push_back(constantInput.m_registerId);

                D3D12_ROOT_PARAMETER parameter;
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                parameter.ShaderVisibility = ConvertShaderStageMask(spaceGroup.m_stageMask);
                parameter.Descriptor.ShaderRegister = constantInput.m_registerId;
                parameter.Descriptor.RegisterSpace  = constantInput.m_spaceId;

                m_spaceCBVBindings[index].m_rootIndices.push_back(RootParameterIndex(parameters.size()));
                parameters.push_back(parameter);
            }
        }
    }

    void PipelineLayout::BuildSpaceGroupResources(
        const PipelineLayoutDescriptor* desc,
        eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[]
    )
    {
        uint32_t groupCount = static_cast<uint32_t>(desc->GetSpaceGroupCount());
        for (uint32_t index = 0; index < groupCount; ++index)
        {
            const RHI::ShaderInputGroup& spaceGroup = desc->GetSpaceGroup(index);

            for (auto handle: spaceGroup.m_shaderInputs)
            {
                if (handle.m_type != ShaderInputType::Buffer && handle.m_type != ShaderInputType::Image)
                {
                    continue;
                }

                if (handle.m_type == ShaderInputType::Buffer)
                {
                    const RHI::ShaderInputBufferDescriptor& bufferInput = desc->GetBufferDescriptor(handle.m_index);
                    D3D12_DESCRIPTOR_RANGE descriptorRange;
                    descriptorRange.RegisterSpace  = bufferInput.m_spaceId;
                    descriptorRange.NumDescriptors = bufferInput.m_count;
                    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    descriptorRange.BaseShaderRegister = bufferInput.m_registerId;

                    switch (bufferInput.m_access)
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

                    descriptorRanges[index].push_back(descriptorRange);
                }

                if (handle.m_type == ShaderInputType::Image)
                {
                    const RHI::ShaderInputImageDescriptor& imageInput = desc->GetImageDescriptor(handle.m_index);
                    D3D12_DESCRIPTOR_RANGE descriptorRange;
                    descriptorRange.RegisterSpace  = imageInput.m_spaceId;
                    descriptorRange.NumDescriptors = imageInput.m_count;
                    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    descriptorRange.BaseShaderRegister = imageInput.m_registerId;

                    switch (imageInput.m_access)
                    {
                    case RHI::ShaderInputImageAccess::Read:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;

                    case RHI::ShaderInputImageAccess::ReadWrite:
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    }

                    descriptorRanges[index].push_back(descriptorRange);
                }
            }

            if (descriptorRanges[index].empty())
            {
                continue;
            }

            D3D12_ROOT_PARAMETER parameter;
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = ConvertShaderStageMask(spaceGroup.m_stageMask);
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRanges[index].size());
            parameter.DescriptorTable.pDescriptorRanges = descriptorRanges[index].data();

            m_spaceTableBindings[index].m_resourceTable = RootParameterIndex(parameters.size());
            parameters.push_back(parameter);
        }
    }

    void PipelineLayout::BuildSpaceGroupSamplers(
        const PipelineLayoutDescriptor* desc,
        eastl::vector<D3D12_ROOT_PARAMETER>& parameters,
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[])
    {
        for (uint32_t index = 0; index < desc->GetSpaceGroupCount(); ++index)
        {
            const RHI::ShaderInputGroup& spaceGroup = desc->GetSpaceGroup(index);

            for (auto handle : spaceGroup.m_shaderInputs)
            {
                if (handle.m_type != ShaderInputType::Sampler)
                {
                    continue;
                }

                const RHI::ShaderInputSamplerDescriptor& samplerInput = desc->GetSamplerDescriptor(handle.m_index);
                D3D12_DESCRIPTOR_RANGE descriptorRange;
                descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                descriptorRange.RegisterSpace = samplerInput.m_spaceId;
                descriptorRange.NumDescriptors = samplerInput.m_count;
                descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                descriptorRange.BaseShaderRegister = samplerInput.m_registerId;

                descriptorRanges[index].push_back(descriptorRange);
            }

            if (descriptorRanges[index].empty())
            {
                continue;
            }

            D3D12_ROOT_PARAMETER parameter;
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = ConvertShaderStageMask(spaceGroup.m_stageMask);
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(descriptorRanges[index].size());
            parameter.DescriptorTable.pDescriptorRanges = descriptorRanges[index].data();

            m_spaceTableBindings[index].m_samplerTable = RootParameterIndex(parameters.size());
            parameters.push_back(parameter);
        }
    }

    void PipelineLayout::BuildSpaceGroupStaticSamplers(
        const PipelineLayoutDescriptor* desc,
        eastl::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers)
    {
        for (uint32_t i = 0; i < desc->GetStaticSamplerCount(); ++i)
        {
            const RHI::ShaderInputStaticSamplerDescriptor& samplerDesc = desc->GetStaticSamplerDescriptor(i);
            const RHI::ShaderStageMask stageMask = desc->GetStaticSamplerStageMask(i);

            D3D12_STATIC_SAMPLER_DESC d3dDesc;
            ConvertStaticSampler(
                samplerDesc.m_samplerState,
                samplerDesc.m_registerId,
                samplerDesc.m_spaceId,
                ConvertShaderStageMask(stageMask),
                d3dDesc);

            staticSamplers.push_back(d3dDesc);
        }
    }
    ////////////////////////////////////////////////////////////////

    void PipelineLayout::Init(Device& device, const RHI::PipelineLayoutDescriptor& descriptor)
    {
        m_hash = descriptor.GetHash();
        m_d3d12Device = device.GetDX12Device();
        m_layoutDescriptor = &descriptor;

        const uint32_t groupLayoutCount = static_cast<uint32_t>(descriptor.GetShaderResourceLayoutCount());
        ASSERT(groupLayoutCount <= RHI::Limits::Pipeline::ShaderResourceCountMax, "Exceeded ShaderResourceGroupLayout count limit.");

        eastl::vector<D3D12_ROOT_PARAMETER> parameters;
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[RHI::Limits::Pipeline::ShaderResourceCountMax];
        ////////////////////////////
        // not use
        eastl::vector<D3D12_DESCRIPTOR_RANGE> unboundedArraydescriptorRanges[RHI::Limits::Pipeline::ShaderResourceCountMax];
        ////////////////////////////
        eastl::vector<D3D12_DESCRIPTOR_RANGE> samplerDescriptorRanges[RHI::Limits::Pipeline::ShaderResourceCountMax];
        eastl::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;

        const PipelineLayoutDescriptor* dx12Descriptor = static_cast<const PipelineLayoutDescriptor*>(&descriptor);

        // Initialise old-path mapping tables (no-op for new path since groupLayoutCount == 0)
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

        if (descriptor.UsesShaderInputPath())
        {
            m_spaceRootParams.resize(descriptor.GetSpaceGroupCount());
            m_spaceCBVBindings.resize(descriptor.GetSpaceGroupCount());
            m_spaceTableBindings.resize(descriptor.GetSpaceGroupCount());

            BuildSpaceGroupConstants(dx12Descriptor, parameters);
            BuildSpaceGroupResources(dx12Descriptor, parameters, descriptorRanges);
            BuildSpaceGroupSamplers(dx12Descriptor, parameters, samplerDescriptorRanges);
            BuildSpaceGroupStaticSamplers(dx12Descriptor, staticSamplers);
        }
        else
        {
            // Next, front-load by frequency the SRG Constants. Each SRG with Constants adds a constant buffer entry as root parameters of the root signature.
            BuildShaderResourceConstants(dx12Descriptor, indexesSortedByFrequency, parameters);

            // Next, process the remaining descriptor tables by frequency.
            BuildShaderResourceBuffersAndImages(dx12Descriptor, indexesSortedByFrequency, parameters, descriptorRanges);

            // Next, process the dynamic sampler descriptor tables by frequency. Sampler can't be mixed with other resources
            BuildShaderResourceSamplers(dx12Descriptor, indexesSortedByFrequency, parameters, samplerDescriptorRanges);

            // Last, process by frequency the static samplers
            BuildStaticSamplers(dx12Descriptor, indexesSortedByFrequency, parameters, staticSamplers);
        }

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        rootSignatureDesc.NumParameters = static_cast<uint32_t>(parameters.size());
        rootSignatureDesc.pParameters = parameters.data();
        rootSignatureDesc.NumStaticSamplers = static_cast<uint32_t>(staticSamplers.size());
        rootSignatureDesc.pStaticSamplers = staticSamplers.data();

        Microsoft::WRL::ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
        D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, pOutBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
        ASSERT(pOutBlob, "Failed to serialize root signature: ErrorBlob [{}]", pErrorBlob ? reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()) : "No error data returned");

        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
        HRESULT result = m_d3d12Device->CreateRootSignature(1, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature.GetAddressOf()));
        ASSERT(result == S_OK, "Failed to create root signature");
        m_signature = rootSignature.Get();
        
        DeviceObject::Init(device);
    }

    const RootParameterBinding& PipelineLayout::GetSpaceBinding(uint32_t spaceIndex) const
    {
        return m_spaceRootParams[spaceIndex];
    }

    size_t PipelineLayout::GetSpaceGroupCount() const
    {
        return m_spaceRootParams.size();
    }

    const SpaceCBVBinding& PipelineLayout::GetSpaceCBVBinding(uint32_t spaceIndex) const
    {
        return m_spaceCBVBindings[spaceIndex];
    }

    const SpaceTableBinding& PipelineLayout::GetSpaceTableBinding(uint32_t spaceIndex) const
    {
        return m_spaceTableBindings[spaceIndex];
    }

    bool PipelineLayout::HasRootConstants() const
    {
        return m_hasRootConstants;
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