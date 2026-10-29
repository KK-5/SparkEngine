#include "AccelerationStructure.h"

#include <ID3D12Factory.h>
#include <Device/Device.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/Buffer/Buffer.h>

namespace Spark::RHI::DX12
{
    GpuVirtualAddress AccelerationStructure::GetGpuAddress() const
    {
        return m_gpuAddress;
    }

    DescriptorHandle AccelerationStructure::GetReadDescriptor() const
    {
        return m_readDescriptor;
    }

    uint64_t AccelerationStructure::GetDeviceAddress() const
    {
        return m_gpuAddress;
    }

    RHI::ResultCode AccelerationStructure::InitInternal(
        RHI::Device& deviceBase, const RHI::AccelerationStructureDescriptor& descriptor)
    {
        Device& device = static_cast<Device&>(deviceBase);
        const Buffer& buffer = static_cast<const Buffer&>(*descriptor.m_buffer);

        m_gpuAddress = buffer.GetMemoryView().GetGpuAddress() + descriptor.m_offset;

        DescriptorContext& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorContext.CreateAccelerationStructureView(m_gpuAddress, m_readDescriptor);
        if (m_readDescriptor.IsNull())
        {
            m_gpuAddress = 0;
            return RHI::ResultCode::OutOfMemory;
        }

        return RHI::ResultCode::Success;
    }

    void AccelerationStructure::ShutdownInternal()
    {
        Device& device = static_cast<Device&>(GetDevice());
        DescriptorContext& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);
        descriptorContext.ReleaseDescriptor(m_readDescriptor);
        m_gpuAddress = 0;
    }
}
