#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcommon.h>
#include <wrl.h>

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
    using CpuVirtualAddress = uint8_t*;
}