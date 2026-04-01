/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineState.h"

#include <EASTL/vector.h>
#include <Log/SpdLogSystem.h>

#include <ID3D12Factory.h>
#include <Device/Device.h>
#include <Conversions.h>

#include "ShaderStageFunction.h"
#include "PipelineLibrary.h"

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

    D3D12_SHADER_BYTECODE D3D12BytecodeFromView(RHI::ShaderByteCodeView view)
    {
        return D3D12_SHADER_BYTECODE{ view.data(), view.size() };
    }

    RHI::ResultCode PipelineState::InitInternal(RHI::Device& deviceBase, const RHI::PipelineStateDescriptorForDraw& descriptor, RHI::PipelineLibrary* pipelineLibraryBase)
    {
        Device& device = static_cast<Device&>(deviceBase);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.NodeMask = 1;
        pipelineStateDesc.SampleMask = 0xFFFFFFFFu;
        pipelineStateDesc.SampleDesc.Count = descriptor.m_renderStates.m_multisampleState.m_samples;
        pipelineStateDesc.SampleDesc.Quality = descriptor.m_renderStates.m_multisampleState.m_quality;

        // Shader state.
        Ptr<PipelineLayout> pipelineLayout = Service<ID3D12FactoryInterface>::Get()->CreatePipelineLayout();
        pipelineLayout->Init(device, *descriptor.m_pipelineLayoutDescriptor);
        pipelineStateDesc.pRootSignature = pipelineLayout->Get();

        // eastl::vector<ShaderByteCode> shaderByteCodeCache;
        const ShaderStageFunction* vertexFunction = static_cast<const ShaderStageFunction*>(descriptor.m_vertexFunction.get());
        pipelineStateDesc.VS = D3D12BytecodeFromView(vertexFunction->GetByteCode());
        if (descriptor.m_geometryFunction)
        {
            const ShaderStageFunction* geometryFunction = static_cast<const ShaderStageFunction*>(descriptor.m_geometryFunction.get());
            pipelineStateDesc.GS = D3D12BytecodeFromView(geometryFunction->GetByteCode());
        }
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

        PipelineLibrary* pipelineLibrary = static_cast<PipelineLibrary*>(pipelineLibraryBase);

        Ptr<ID3D12PipelineState> pipelineState;
        if (pipelineLibrary && pipelineLibrary->IsInitialized())
        {
            pipelineState = pipelineLibrary->CreateGraphicsPipelineState(static_cast<uint64_t>(descriptor.GetHash()), pipelineStateDesc);
        }
        else
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateComPtr;
            HRESULT result = device.GetDX12Device()->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(pipelineStateComPtr.GetAddressOf()));
            if (SUCCEEDED(result))
            {
                pipelineState = pipelineStateComPtr.Get();
            }
        }

        if (pipelineState)
        {
            m_pipelineLayout = eastl::move(pipelineLayout);
            m_pipelineState = eastl::move(pipelineState);
            m_pipelineStateData.m_type = RHI::PipelineStateType::Draw;
            m_pipelineStateData.m_drawData = PipelineStateDrawData{ descriptor.m_renderStates.m_multisampleState, descriptor.m_inputStreamLayout.GetTopology() };
            return RHI::ResultCode::Success;
        }
        else
        {
            LOG_ERROR("[PipelineState] Failed to compile graphics pipeline state. Check the D3D12 debug layer for more info.");
            return RHI::ResultCode::Fail;
        }
    }

    RHI::ResultCode PipelineState::InitInternal(RHI::Device& deviceBase, const RHI::PipelineStateDescriptorForDispatch& descriptor, RHI::PipelineLibrary* pipelineLibraryBase)
    {
        Device& device = static_cast<Device&>(deviceBase);

        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.NodeMask = 1;

        // Shader state.
        Ptr<PipelineLayout> pipelineLayout = Service<ID3D12FactoryInterface>::Get()->CreatePipelineLayout();
        pipelineLayout->Init(device, *descriptor.m_pipelineLayoutDescriptor);
        pipelineStateDesc.pRootSignature = pipelineLayout->Get();

        const ShaderStageFunction* computeFunction = static_cast<const ShaderStageFunction*>(descriptor.m_computeFunction.get());
        pipelineStateDesc.CS = D3D12BytecodeFromView(computeFunction->GetByteCode());

        PipelineLibrary* pipelineLibrary = static_cast<PipelineLibrary*>(pipelineLibraryBase);

        Ptr<ID3D12PipelineState> pipelineState;
        if (pipelineLibrary && pipelineLibrary->IsInitialized())
        {
            pipelineState = pipelineLibrary->CreateComputePipelineState(static_cast<uint64_t>(descriptor.GetHash()), pipelineStateDesc);
        }
        else
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateComPtr;
            HRESULT result = device.GetDX12Device()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(pipelineStateComPtr.GetAddressOf()));
            if (SUCCEEDED(result))
            {
                pipelineState = pipelineStateComPtr.Get();
            }
        }

        if (pipelineState)
        {
            m_pipelineLayout = eastl::move(pipelineLayout);
            m_pipelineState = eastl::move(pipelineState);
            m_pipelineStateData.m_type = RHI::PipelineStateType::Dispatch;
            return RHI::ResultCode::Success;
        }
        else
        {
            LOG_ERROR("[PipelineState] Failed to compile graphics pipeline state. Check the D3D12 debug layer for more info.");
            return RHI::ResultCode::Fail;
        }
    }

    RHI::ResultCode PipelineState::InitInternal(RHI::Device& deviceBase, const RHI::PipelineStateDescriptorForRayTracing& descriptor, RHI::PipelineLibrary* pipelineLibrary)
    {
        Device& device = static_cast<Device&>(deviceBase);

        Ptr<PipelineLayout> pipelineLayout = Service<ID3D12FactoryInterface>::Get()->CreatePipelineLayout();
        pipelineLayout->Init(device, *descriptor.m_pipelineLayoutDescriptor);

        m_pipelineLayout = eastl::move(pipelineLayout);
        m_pipelineStateData.m_type = RHI::PipelineStateType::RayTracing;

        return RHI::ResultCode::Success;
    }

    void PipelineState::ShutdownInternal()
    {
        // ray tracing shaders do not have a traditional pipeline state object
        if (m_pipelineStateData.m_type != RHI::PipelineStateType::RayTracing)
        {
            auto ID3D12Factory = Service<ID3D12FactoryInterface>::Get();
            ASSERT(ID3D12Factory, "ID3D12Factory is null!");
            ID3D12Factory->QueueForRelease(static_cast<Device&>(GetDevice()), eastl::move(m_pipelineState));
        }

        m_pipelineState = nullptr;
        m_pipelineLayout = nullptr;
    }
}