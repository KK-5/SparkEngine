#pragma once

#include <RHI/ClearValue.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <DX12.h>
#include <ReleaseQueue.h>
#include <MemoryView.h>

#include "PhysicalDevice.h"

namespace Spark::RHI::DX12
{
    class Device final : public RHI::Device
    {
    public:
        ID3D12DeviceX* GetDX12Device();

        //! Queues a DX12 COM object for release (by taking a reference) after the current frame has flushed
        //! through the GPU.
        //! Usually called in object ShutdownInternal function.
        void QueueForRelease(Ptr<ID3D12Object> dx12Object);

        const PhysicalDevice& GetPhysicalDevice() const;

        // return the binding slot of the bindless srg
        // uint32_t GetBindlessSrgSlot() const;
    
    private:
        //////////////////////////////
        /// RHI::Device override
        RHI::ResultCode InitInternal(RHI::PhysicalDevice& physicalDevice) override;
        void ShutdownInternal() override;
        // RHI::ResultCode BeginFrameInternal() override;
        //void EndFrameInternal() override;
        //void WaitForIdleInternal() override;
        RHI::ResultCode InitializeLimits() override;
        void FillFormatsCapabilitiesInternal(FormatCapabilitiesList& formatsCapabilities) override;
        // void PreShutdown() override;
        //////////////////////////////

        /// @brief init m_features and m_limits
        void InitFeatures();

        Ptr<ID3D12DeviceX> m_dx12Device;
        Ptr<IDXGIAdapterX> m_dxgiAdapter;
        Ptr<IDXGIFactoryX> m_dxgiFactory;

        D3D12ObjReleaseQueue    m_objReleaseQueue;
    };
}