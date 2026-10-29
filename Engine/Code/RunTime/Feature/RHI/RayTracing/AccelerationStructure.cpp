#include "AccelerationStructure.h"

#include <Log/ILogSystem.h>
#include <RHI/RHILimits.h>

namespace Spark::RHI
{
    ResultCode AccelerationStructure::Init(Device& device, const AccelerationStructureDescriptor& descriptor)
    {
        if (Validation::isEnabled && !ValidateForInit(descriptor))
        {
            return ResultCode::InvalidArgument;
        }

        m_type   = descriptor.m_type;
        m_buffer = descriptor.m_buffer;
        m_offset = descriptor.m_offset;
        m_size   = descriptor.m_size;

        const ResultCode result = InitInternal(device, descriptor);
        if (result != ResultCode::Success)
        {
            m_buffer = nullptr;
            return result;
        }

        DeviceObject::Init(device);
        return ResultCode::Success;
    }

    void AccelerationStructure::Shutdown()
    {
        if (IsInitialized())
        {
            ShutdownInternal();
            m_buffer = nullptr;
        }
        DeviceObject::Shutdown();
    }

    AccelerationStructureType AccelerationStructure::GetType() const
    {
        return m_type;
    }

    const Buffer& AccelerationStructure::GetBuffer() const
    {
        return *m_buffer;
    }

    uint64_t AccelerationStructure::GetOffset() const
    {
        return m_offset;
    }

    uint64_t AccelerationStructure::GetSize() const
    {
        return m_size;
    }

    bool AccelerationStructure::ValidateForInit(const AccelerationStructureDescriptor& descriptor) const
    {
        if (IsInitialized())
        {
            LOG_ERROR("[AccelerationStructure] Already initialized.");
            return false;
        }

        const Buffer* buffer = descriptor.m_buffer;
        if (buffer == nullptr || !buffer->IsInitialized())
        {
            LOG_ERROR("[AccelerationStructure] The buffer is null or not initialized.");
            return false;
        }

        const BufferDescriptor& bufferDescriptor = buffer->GetDescriptor();
        if (bufferDescriptor.m_bindFlags != BufferBindFlags::RayTracingAccelerationStructure)
        {
            LOG_ERROR("[AccelerationStructure] Buffer {} must be bound with RayTracingAccelerationStructure alone.",
                      buffer->GetName().GetCStr());
            return false;
        }

        if (descriptor.m_offset % Alignment::AccelerationStructure != 0)
        {
            LOG_ERROR("[AccelerationStructure] Offset {} is not a multiple of {}.",
                      descriptor.m_offset, Alignment::AccelerationStructure);
            return false;
        }

        if (descriptor.m_size == 0 || descriptor.m_offset + descriptor.m_size > bufferDescriptor.m_byteCount)
        {
            LOG_ERROR("[AccelerationStructure] Range [{}, +{}) is empty or exceeds buffer {} of {} bytes.",
                      descriptor.m_offset, descriptor.m_size, buffer->GetName().GetCStr(), bufferDescriptor.m_byteCount);
            return false;
        }

        return true;
    }
}
