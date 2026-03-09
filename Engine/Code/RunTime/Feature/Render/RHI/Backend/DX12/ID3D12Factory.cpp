#include "ID3D12Factory.h"

#include "DX12.h"
#include "Device/Device.h"
#include "Device/PhysicalDevice.h"

#include "Resource/Buffer/Buffer.h"

namespace Spark::RHI::DX12
{
    RHI::ResultCode ID3D12Factory::Init()
    {
        BufferObjectPool::Descriptor bufferPoolDesc;
        bufferPoolDesc.m_collectLatency = 0;
        m_bufferObjectPool.Init(bufferPoolDesc);

        return RHI::ResultCode::Success;
    }

    void ID3D12Factory::Shutdown()
    {
        m_bufferObjectPool.Shutdown();
    }

    Ptr<PhysicalDevice> ID3D12Factory::CreatePhysicalDevice()
    {
        return new PhysicalDevice();
    }

    Ptr<Device> ID3D12Factory::CreateDX12Device()
    {
        return new Device();
    }

    RHI::PhysicalDeviceList ID3D12Factory::EnumeratePhysicalDevices()
    {
        RHI::PhysicalDeviceList physicalDeviceList;

        ComPtr<IDXGIFactoryX> dxgiFactory;
        HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

        ComPtr<IDXGIAdapter> dxgiAdapter;
        for (uint32_t i = 0; dxgiFactory->EnumAdapters(i, dxgiAdapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            ComPtr<IDXGIAdapterX> dxgiAdapterX;
            dxgiAdapter->QueryInterface(IID_PPV_ARGS(dxgiAdapterX.GetAddressOf()));

            DXGI_ADAPTER_DESC1 adapterDesc;
            dxgiAdapterX->GetDesc1(&adapterDesc);

            // Skip devices with software rasterization
            if(CheckBitsAny(adapterDesc.Flags, static_cast<UINT>(DXGI_ADAPTER_FLAG::DXGI_ADAPTER_FLAG_SOFTWARE)))
            {
                continue;
            }

            Ptr<PhysicalDevice> physicalDevice = CreatePhysicalDevice();
            physicalDevice->Init(dxgiFactory.Get(), dxgiAdapterX.Get());
            physicalDeviceList.push_back(physicalDevice);
        }

        return physicalDeviceList;
    }

    Ptr<RHI::Device> ID3D12Factory::CreateDevice()
    {
        return CreateDX12Device();
    }

    Ptr<RHI::Buffer> ID3D12Factory::CreateBuffer()
    {
        return m_bufferObjectPool.Allocate();
    }

    void ID3D12Factory::QueueForRelease(Buffer* buffer)
    {
        m_bufferObjectPool.DeAllocate(buffer);
    }
}