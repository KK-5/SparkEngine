#include "Device.h"

#include <Log/ILogSystem.h>
#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>

namespace Spark::RHI
{
    bool Device::IsInitialized() const
    {
        return m_physicalDevice != nullptr;
    }

    ResultCode Device::Init(PhysicalDevice& physicalDevice, const DeviceDescriptor& descriptor)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[Device] Device is already initialized.");
                return ResultCode::InvalidOperation;
            }
        }

        m_physicalDevice = &physicalDevice;
        m_descriptor = descriptor;

        ResultCode resultCode = InitInternal(physicalDevice);

        if (resultCode == ResultCode::Success)
        {
            FillFormatsCapabilitiesInternal(m_formatsCapabilities);
            resultCode = InitializeLimits();
        }
        else
        {
            m_physicalDevice = nullptr;
        }

        if (resultCode == ResultCode::Success)
        {
            ObjectCollector<ReleaseQueueTraits>::Descriptor releaseQueueDescriptor;
            releaseQueueDescriptor.m_collectLatency = m_descriptor.m_frameCountMax;
            m_releaseQueue.Init(releaseQueueDescriptor);
            FrameEventBus::Handler::BusConnect();
        }

        return resultCode;
    }

    void Device::Shutdown()
    {
        if (IsInitialized())
        {
            FrameEventBus::Handler::BusDisconnect();
            // Before the backend goes: what is queued may still reach its allocator.
            FlushReleaseQueue();
            ShutdownInternal();
            m_physicalDevice = nullptr;
        }
    }

    void Device::QueueForRelease(Ptr<Object> object)
    {
        m_releaseQueue.QueueForCollect(eastl::move(object));
    }

    void Device::FlushReleaseQueue()
    {
        m_releaseQueue.Collect(true);
    }

    void Device::OnFrameEnd()
    {
        m_releaseQueue.Collect();
        m_frameIndex = (m_frameIndex + 1) % m_descriptor.m_frameCountMax;
    }

    const PhysicalDevice& Device::GetPhysicalDevice() const
    {
        return *m_physicalDevice;
    }

    const DeviceDescriptor& Device::GetDescriptor() const
    {
        return m_descriptor;
    }

    const DeviceFeatures& Device::GetFeatures() const
    {
        return m_features;
    }

    const DeviceLimits& Device::GetLimits() const
    {
        return m_limits;
    }

    FormatCapabilities Device::GetFormatCapabilities(Format format) const
    {
        return m_formatsCapabilities[static_cast<uint32_t>(format)];
    }

    AccelerationStructureBuildSizes Device::GetAccelerationStructureBuildSizes(const AccelerationStructureBuildInputs& inputs) const
    {
        if (Validation::isEnabled && !ValidateAccelerationStructureBuildInputs(*this, inputs))
        {
            return {};
        }
        return GetAccelerationStructureBuildSizesInternal(inputs);
    }
}