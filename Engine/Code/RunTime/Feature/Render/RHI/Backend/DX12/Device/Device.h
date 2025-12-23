#pragma once

#include <Device/Device.h>
#include <Buffer/BufferDescriptor.h>
#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <DX12.h>
#include "PhysicalDevice.h"
#include "../MemoryView.h"

namespace Spark::RHI::DX12
{
    class Device final : public RHI::Device
    {
    public:
        ID3D12DeviceX* GetDevice();

        RHI::ResultCode CreateSwapChain(
            IUnknown* window,
            const DXGI_SWAP_CHAIN_DESCX& swapChainDesc,
            Ptr<IDXGISwapChainX>& swapChain);

        MemoryView CreateD3d12maBuffer(
            const RHI::BufferDescriptor& bufferDescriptor, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType);
    
    private:
        //////////////////////////////
        /// RHI::Device override
        RHI::ResultCode InitInternal(RHI::PhysicalDevice& physicalDevice) override;
        void ShutdownInternal() override;
        RHI::ResultCode BeginFrameInternal() override;
        void EndFrameInternal() override;
        void WaitForIdleInternal() override;
        RHI::ResultCode InitializeLimits() override;
        void FillFormatsCapabilitiesInternal(FormatCapabilitiesList& formatsCapabilities) override;
        void PreShutdown() override;
        //////////////////////////////

        RHI::ResultCode InitD3d12maAllocator();

        /// @brief init m_features and m_limits
        void InitFeatures();


        Ptr<ID3D12DeviceX> m_dx12Device;
        Ptr<IDXGIAdapterX> m_dxgiAdapter;
        Ptr<IDXGIFactoryX> m_dxgiFactory;

        Ptr<D3D12MA::Allocator> m_dx12MemAlloc;

    };
}