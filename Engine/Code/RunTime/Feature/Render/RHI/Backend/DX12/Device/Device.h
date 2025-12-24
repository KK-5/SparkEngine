#pragma once

#include <ClearValue.h>
#include <Device/Device.h>
#include <Resource/Buffer/BufferDescriptor.h>
#include <Resource/Image/ImageDescriptor.h>

#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <DX12.h>

#include "PhysicalDevice.h"
#include "ReleaseQueue.h"
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

        MemoryView CreateD3D12Buffer(
            const RHI::BufferDescriptor& bufferDescriptor, 
            D3D12_RESOURCE_STATES initialState, 
            D3D12_HEAP_TYPE heapType);
        
        MemoryView CreateD3D12Image(
            const RHI::ImageDescriptor& imageDescriptor,
            const RHI::ClearValue* optimizedClearValue,
            D3D12_RESOURCE_STATES initialState,
            D3D12_HEAP_TYPE heapType);
        /*
        MemoryView CreateImageReserved(
            const RHI::ImageDescriptor& imageDescriptor,
            D3D12_RESOURCE_STATES initialState,
            ImageTileLayout& imageTilingInfo);
        */

        //! Queues a DX12 COM object for release (by taking a reference) after the current frame has flushed
        //! through the GPU.
        //! Usually called in object ShutdownInternal function.
        void QueueForRelease(Ptr<ID3D12Object> dx12Object);

        //! Queues the backing Memory instance of a MemoryView for release (by taking a reference) after the
        //! current frame has flushed through the GPU. The reference on the MemoryView itself is not released.
        //! Usually called in object ShutdownInternal function.
        void QueueForRelease(MemoryView& memoryView);

        //! Allocates host memory from the internal frame allocator that is suitable for staging
        //! uploads to the GPU for the current frame. The memory is valid for the lifetime of
        //! the frame and is automatically reclaimed after the frame has completed on the GPU.
        MemoryView AcquireStagingMemory(size_t size, size_t alignment);

        //! Acquires a pipeline layout from the internal cache.
        // ConstPtr<PipelineLayout> AcquirePipelineLayout(const RHI::PipelineLayoutDescriptor& descriptor);

        //! Acquires a new command list for the frame given the hardware queue class. The command list is
        //! automatically reclaimed after the current frame has flushed through the GPU.
        // CommandList* AcquireCommandList(RHI::HardwareQueueClass hardwareQueueClass);

        //! Acquires a sampler from the internal cache.
        // ConstPtr<Sampler> AcquireSampler(const RHI::SamplerState& state);

        const PhysicalDevice& GetPhysicalDevice() const;

        //CommandQueueContext& GetCommandQueueContext();

        //DescriptorContext& GetDescriptorContext();

        //AsyncUploadQueue& GetAsyncUploadQueue();

        // return the binding slot of the bindless srg
        // uint32_t GetBindlessSrgSlot() const;
    
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
        D3D12ObjReleaseQueue    m_objReleaseQueue;
        D3D12MAReleaseQueue     m_D3D12MAReleaseQueue;

    };
}