/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

/*
 * Modified by SparkEngine in 2025
 *  -- ID3D12PipelineLibrary is not currently in use.
 */


#include "PipelineLibrary.h"

#include <Device/Device.h>

namespace Spark::RHI::DX12
{
    RHI::ResultCode PipelineLibrary::InitInternal(RHI::Device& deviceBase, const RHI::PipelineLibraryDescriptor& descriptor)
    {
        Device& device = static_cast<Device&>(deviceBase);
        m_d3d12Device = device.GetDevice();

        return RHI::ResultCode::Success;
    }

    void PipelineLibrary::ShutdownInternal()
    {
        m_d3d12Device = nullptr;
    }

    Ptr<ID3D12PipelineState> PipelineLibrary::CreateGraphicsPipelineState([[maybe_unused]] uint64_t hash, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineStateDesc)
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateComPtr;
        HRESULT hr = m_d3d12Device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(pipelineStateComPtr.GetAddressOf()));
        if (SUCCEEDED(hr))
        {
            return pipelineStateComPtr.Get();
        }
        return nullptr;
    }

    Ptr<ID3D12PipelineState> PipelineLibrary::CreateComputePipelineState([[maybe_unused]] uint64_t hash, const D3D12_COMPUTE_PIPELINE_STATE_DESC& pipelineStateDesc)
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateComPtr;
        HRESULT hr = m_d3d12Device->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(pipelineStateComPtr.GetAddressOf()));
        if (SUCCEEDED(hr))
        {
            return pipelineStateComPtr.Get();
        }
        return nullptr;
    }
}