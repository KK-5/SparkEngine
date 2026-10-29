#pragma once

#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/RayTracing/AccelerationStructure.h>

#include <Descriptor/Descriptor.h>

namespace Spark::RHI::DX12
{
    //! DX12 has no acceleration structure object: it is the GPU address of its range, and
    //! binds as an SRV whose view carries that address.
    class AccelerationStructure final : public RHI::AccelerationStructure
    {
    public:
        GpuVirtualAddress GetGpuAddress() const;

        //! A CPU descriptor copied into descriptor tables when bound.
        DescriptorHandle GetReadDescriptor() const;

        //////////////////////////////////////////////////////////////////////////
        // RHI::AccelerationStructure
        uint64_t GetDeviceAddress() const override;
        //////////////////////////////////////////////////////////////////////////

    private:
        AccelerationStructure() = default;

        friend class DeviceObjectFactory<AccelerationStructure>;

        //////////////////////////////////////////////////////////////////////////
        // RHI::AccelerationStructure
        RHI::ResultCode InitInternal(RHI::Device& device, const RHI::AccelerationStructureDescriptor& descriptor) override;
        void            ShutdownInternal() override;
        //////////////////////////////////////////////////////////////////////////

        GpuVirtualAddress m_gpuAddress = 0;
        DescriptorHandle  m_readDescriptor;
    };
}
