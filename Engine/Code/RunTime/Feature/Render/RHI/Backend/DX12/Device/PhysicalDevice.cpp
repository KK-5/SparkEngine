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
        m_descriptor.m_type = RHI::PhysicalDeviceType::Unknown; /// DXGI can't tell what kind of device this is?!
        m_descriptor.m_vendorId = static_cast<RHI::VendorId>(adapterDesc.VendorId);
        m_descriptor.m_deviceId = adapterDesc.DeviceId;
        /// GPU专用内存
        m_descriptor.m_heapSizePerLevel[static_cast<size_t>(RHI::HeapMemoryLevel::Device)] = adapterDesc.DedicatedVideoMemory;
        /// 系统专用内存
        m_descriptor.m_heapSizePerLevel[static_cast<size_t>(RHI::HeapMemoryLevel::Host)] = adapterDesc.DedicatedSystemMemory;
    }

    void PhysicalDevice::Shutdown() 
    {
        m_dxgiAdapter = nullptr;
        m_dxgiFactory = nullptr;
    }
}