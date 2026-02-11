/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineState.h"

#include <EASTL/vector.h>

#include <D3D12Factory.h>
#include <Device/Device.h>
#include <Conversions.h>

#include "ShaderStageFunction.h"

namespace Spark::RHI::DX12
{
    const PipelineLayout* PipelineState::GetPipelineLayout() const
    {
        return m_pipelineLayout.get();
    }

    ID3D12PipelineState* PipelineState::Get() const
    {
        return m_pipelineState.get();
    }

    const PipelineStateData& PipelineState::GetPipelineStateData() const
    {
        return m_pipelineStateData;
    }

    D3D12_SHADER_BYTECODE D3D12BytecodeFromView(ShaderByteCodeView view)
    {
        return D3D12_SHADER_BYTECODE{ view.data(), view.size() };
    }

    RHI::ResultCode InitInternal(RHI::Device& deviceBase, const RHI::PipelineStateDescriptorForDraw& descriptor, RHI::PipelineLibrary* pipelineLibrary)
    {
        Device& device = static_cast<Device&>(deviceBase);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.NodeMask = 1;
        pipelineStateDesc.SampleMask = 0xFFFFFFFFu;
        pipelineStateDesc.SampleDesc.Count = descriptor.m_renderStates.m_multisampleState.m_samples;
        pipelineStateDesc.SampleDesc.Quality = descriptor.m_renderStates.m_multisampleState.m_quality;

        // Shader state.
        Ptr<PipelineLayout> pipelineLayout = Service<D3D12FactoryInterface>::Get()->CreatePipelineLayout();
        pipelineLayout->Init(device, *descriptor.m_pipelineLayoutDescriptor);

        eastl::vector<ShaderByteCode> shaderByteCodeCache;
        const ShaderStageFunction* vertexFunction = static_cast<const ShaderStageFunction*>(descriptor.m_vertexFunction.get());
        pipelineStateDesc.VS = D3D12BytecodeFromView(vertexFunction->GetByteCode());
        const ShaderStageFunction* geometryFunction = static_cast<const ShaderStageFunction*>(descriptor.m_geometryFunction.get());
        pipelineStateDesc.GS = D3D12BytecodeFromView(geometryFunction->GetByteCode());
        const ShaderStageFunction* fragmentFunction = static_cast<const ShaderStageFunction*>(descriptor.m_fragmentFunction.get());
        pipelineStateDesc.PS = D3D12BytecodeFromView(fragmentFunction->GetByteCode());

        // RTV/DSV Format
        const RHI::RenderAttachmentConfiguration& renderAttachmentConfiguration = descriptor.m_renderAttachmentConfiguration;
        pipelineStateDesc.DSVFormat = ConvertFormat(renderAttachmentConfiguration.GetDepthStencilFormat());
        pipelineStateDesc.NumRenderTargets = renderAttachmentConfiguration.GetRenderTargetCount();
        for (uint32_t targetIdx = 0; targetIdx < pipelineStateDesc.NumRenderTargets; ++targetIdx)
        {
            pipelineStateDesc.RTVFormats[targetIdx] = ConvertFormat(renderAttachmentConfiguration.GetRenderTargetFormat(targetIdx));
        }

        // Input element
        eastl::vector<D3D12_INPUT_ELEMENT_DESC> inputElements = ConvertInputElements(descriptor.m_inputStreamLayout);
        pipelineStateDesc.InputLayout.NumElements = uint32_t(inputElements.size());
        pipelineStateDesc.InputLayout.pInputElementDescs = inputElements.data();
        pipelineStateDesc.PrimitiveTopologyType = ConvertToTopologyType(descriptor.m_inputStreamLayout.GetTopology());

        // Render state
        pipelineStateDesc.BlendState = ConvertBlendState(descriptor.m_renderStates.m_blendState);
        pipelineStateDesc.RasterizerState = ConvertRasterState(descriptor.m_renderStates.m_rasterState);
        pipelineStateDesc.DepthStencilState = ConvertDepthStencilState(descriptor.m_renderStates.m_depthStencilState);

    }
}