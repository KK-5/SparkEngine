/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcommon.h>
#include <wrl.h>
#include <3rdParty/D3DX12/d3dx12.h>

namespace Spark::RHI::DX12
{
    template<typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;


    using ID3D12CommandAllocatorX = ID3D12CommandAllocator;
    using ID3D12CommandQueueX = ID3D12CommandQueue;
    using ID3D12DeviceX = ID3D12Device7;
    using ID3D12PipelineLibraryX = ID3D12PipelineLibrary1;
    using ID3D12PipelineStateX = ID3D12PipelineState;
    using ID3D12GraphicsCommandListX = ID3D12GraphicsCommandList4;

    using IDXGIAdapterX = IDXGIAdapter4;
    using IDXGIFactoryX = IDXGIFactory7;
    using IDXGISwapChainX = IDXGISwapChain4;
    using DXGI_SWAP_CHAIN_DESCX = DXGI_SWAP_CHAIN_DESC1;

    using GpuDescriptorHandle = D3D12_GPU_DESCRIPTOR_HANDLE;
    using GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS;
    using CpuVirtualAddress = uint8_t*;   // 保证指针按1个字节自增

    DXGI_FORMAT GetBaseFormat(DXGI_FORMAT defaultFormat);
    DXGI_FORMAT GetSRVFormat(DXGI_FORMAT defaultFormat);
    DXGI_FORMAT GetUAVFormat(DXGI_FORMAT defaultFormat);
    DXGI_FORMAT GetDSVFormat(DXGI_FORMAT defaultFormat);
    DXGI_FORMAT GetStencilFormat(DXGI_FORMAT defaultFormat);

    namespace Alignment
    {
        enum
        {
            Buffer = 16,
            Constant = 256,
            Image = 512,
            CommittedBuffer = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
        };
    }
}