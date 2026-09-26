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
#include <Log/ILogSystem.h>

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

    void PipelineLayout::BuildRootConstants(const PipelineLayoutDescriptor* desc, eastl::vector<D3D12_ROOT_PARAMETER>& parameters)
    {
        const RHI::ConstantsLayout* layout = desc->GetRootConstantsLayout();
        if (!layout)
        {
            return;
        }

        // RHI::PipelineLayoutDescriptor::Finalize made them one block: one register, one space.
        const eastl::span<const RHI::ShaderInputConstantDescriptor> inputs = layout->GetShaderInputList();
        RHI::ShaderStageMask stageMask = RHI::ShaderStageMask::None;
        for (const RHI::ShaderInputConstantDescriptor& input : inputs)
        {
            stageMask = stageMask | input.m_stageMask;
        }

        m_rootConstantsByteCount          = layout->GetDataSize();
        m_rootConstantsRootParameterIndex = RootParameterIndex(parameters.size());

        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.Num32BitValues = (m_rootConstantsByteCount + 3) / 4;
        parameter.Constants.ShaderRegister = inputs[0].m_registerId;
        parameter.Constants.RegisterSpace  = inputs[0].m_spaceId;
        parameter.ShaderVisibility         = ConvertShaderStageMask(stageMask);
        parameters.push_back(parameter);
    }

    void PipelineLayout::ValidateRootSignatureCost(const eastl::vector<D3D12_ROOT_PARAMETER>& parameters) const
    {
        // A table costs one DWORD, a root descriptor two, root constants one per value.
        uint32_t cost = 0;
        for (const D3D12_ROOT_PARAMETER& parameter : parameters)
        {
            switch (parameter.ParameterType)
            {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                cost += 1;
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                cost += parameter.Constants.Num32BitValues;
                break;
            default:
                cost += 2;
                break;
            }
        }
        ASSERT(cost <= D3D12_MAX_ROOT_COST,
            "[PipelineLayout] The root signature costs {} DWORDs; DX12 allows {}.", cost, D3D12_MAX_ROOT_COST);
    }

    void PipelineLayout::BuildSpaceGroupConstants(
            const PipelineLayoutDescriptor* desc,
            eastl::vector<D3D12_ROOT_PARAMETER>& parameters
    )
    {
        // Pure consumer of RHI-layer m_constantBuffers — dedup / byte layout already done.
        // We just generate one root CBV per slot, in the same iteration order.
        const uint32_t groupCount = static_cast<uint32_t>(desc->GetSpaceGroupCount());
        for (uint32_t index = 0; index < groupCount; ++index)
        {
            const RHI::ShaderInputGroup& spaceGroup = desc->GetSpaceGroup(index);
            SpaceCBVBinding& cbv = m_spaceCBVBindings[index];

            for (const RHI::ConstantBufferLayout& slot : spaceGroup.m_constantBuffers)
            {
                D3D12_ROOT_PARAMETER parameter{};
                parameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
                parameter.ShaderVisibility          = ConvertShaderStageMask(spaceGroup.m_stageMask);
                parameter.Descriptor.ShaderRegister = slot.m_registerId;
                parameter.Descriptor.RegisterSpace  = spaceGroup.m_spaceId;

                cbv.m_rootIndices.push_back(RootParameterIndex(parameters.size()));
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

    void PipelineLayout::Init(Device& device, const RHI::PipelineLayoutDescriptor& descriptor)
    {
        m_hash = descriptor.GetHash();
        m_d3d12Device = device.GetDX12Device();
        m_layoutDescriptor = &descriptor;

        eastl::vector<D3D12_ROOT_PARAMETER> parameters;
        eastl::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges[RHI::Limits::Pipeline::ShaderInputGroupCountMax];
        eastl::vector<D3D12_DESCRIPTOR_RANGE> samplerDescriptorRanges[RHI::Limits::Pipeline::ShaderInputGroupCountMax];
        eastl::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;

        const PipelineLayoutDescriptor* dx12Descriptor = static_cast<const PipelineLayoutDescriptor*>(&descriptor);

        m_spaceCBVBindings.resize(descriptor.GetSpaceGroupCount());
        m_spaceTableBindings.resize(descriptor.GetSpaceGroupCount());

        // First: the earlier a root parameter, the cheaper it is to change on some hardware.
        BuildRootConstants(dx12Descriptor, parameters);
        BuildSpaceGroupConstants(dx12Descriptor, parameters);
        BuildSpaceGroupResources(dx12Descriptor, parameters, descriptorRanges);
        BuildSpaceGroupSamplers(dx12Descriptor, parameters, samplerDescriptorRanges);
        BuildSpaceGroupStaticSamplers(dx12Descriptor, staticSamplers);

        ValidateRootSignatureCost(parameters);

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        // SM6.6 dynamic resources: let shaders index the shader-visible CBV_SRV_UAV heap
        // directly via ResourceDescriptorHeap[i] — the material-texture bindless path
        // (TODO_MaterialSystemPlan.md B.6). No descriptor range / root param needed; the
        // flag alone opens heap-direct indexing. Free for pipelines that don't use it.
        // Gated on device support so serialization can't fail on pre-SM6.6/tier<3 hardware.
        if (device.GetFeatures().m_bindless)
        {
            rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
        }
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

    const SpaceCBVBinding& PipelineLayout::GetSpaceCBVBinding(uint32_t spaceIndex) const
    {
        return m_spaceCBVBindings[spaceIndex];
    }

    const SpaceTableBinding& PipelineLayout::GetSpaceTableBinding(uint32_t spaceIndex) const
    {
        return m_spaceTableBindings[spaceIndex];
    }

    int32_t PipelineLayout::FindSpaceIndexBySpaceId(uint32_t spaceId) const
    {
        const size_t count = m_layoutDescriptor->GetSpaceGroupCount();
        for (size_t i = 0; i < count; ++i)
        {
            if (m_layoutDescriptor->GetSpaceGroup(i).m_spaceId == spaceId)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    bool PipelineLayout::HasRootConstants() const
    {
        return m_rootConstantsByteCount > 0;
    }

    RootParameterIndex PipelineLayout::GetRootConstantsRootParameterIndex() const
    {
        return m_rootConstantsRootParameterIndex;
    }

    uint32_t PipelineLayout::GetRootConstantsByteCount() const
    {
        return m_rootConstantsByteCount;
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