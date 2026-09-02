#include "Device.h"

#include <Log/ILogSystem.h>

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

        return resultCode;
    }

    void Device::Shutdown()
    {
        if (IsInitialized())
        {
            ShutdownInternal();
            m_physicalDevice = nullptr;
        }
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

    void Device::BeginFrame(uint32_t frameIndex)
    {
        ASSERT(frameIndex < m_descriptor.m_frameCountMax,
            "[Device] Frame index {} is outside the {} in-flight frames.",
            frameIndex, m_descriptor.m_frameCountMax);

        m_frameIndex = frameIndex;
    }
}