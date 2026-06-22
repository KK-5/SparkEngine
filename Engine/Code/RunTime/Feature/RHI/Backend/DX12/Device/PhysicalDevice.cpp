#include "PhysicalDevice.h"

namespace Spark::RHI::DX12
{
    IDXGIFactoryX* PhysicalDevice::GetFactory() const
    {
        return m_dxgiFactory.get();
    }

    IDXGIAdapterX* PhysicalDevice::GetAdapter() const
    {
        return m_dxgiAdapter.get();
    }

    void PhysicalDevice::Init(IDXGIFactoryX* factory, IDXGIAdapterX* adapter)
    {
        m_dxgiFactory = factory;
        m_dxgiAdapter = adapter;

        DXGI_ADAPTER_DESC adapterDesc;
        adapter->GetDesc(&adapterDesc);

        eastl::string description;
        eastl::wstring adapterDescStr = adapterDesc.Description;
        description.append_convert(adapterDescStr);

        m_descriptor.m_description = eastl::move(description);
        m_descriptor.m_vendorId = static_cast<RHI::VendorId>(adapterDesc.VendorId);
        m_descriptor.m_deviceId = adapterDesc.DeviceId;
        m_descriptor.m_heapSizePerLevel[static_cast<size_t>(RHI::HeapMemoryLevel::Device)] = adapterDesc.DedicatedVideoMemory;
        m_descriptor.m_heapSizePerLevel[static_cast<size_t>(RHI::HeapMemoryLevel::Host)] = adapterDesc.DedicatedSystemMemory;

        // DXGI doesn't expose device type directly. Heuristic: discrete GPUs
        // have their own VRAM; integrated GPUs share system memory and report
        // little or no dedicated video memory.
        if (adapterDesc.DedicatedVideoMemory > 0)
        {
            m_descriptor.m_type = RHI::PhysicalDeviceType::GpuDiscrete;
        }
        else
        {
            m_descriptor.m_type = RHI::PhysicalDeviceType::GpuIntegrated;
        }
    }

    void PhysicalDevice::Shutdown() 
    {
        m_dxgiAdapter = nullptr;
        m_dxgiFactory = nullptr;
    }
}