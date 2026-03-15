#include "Device.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    bool Device::IsInitialized() const
    {
        return m_physicalDevice != nullptr;
    }

    ResultCode Device::Init(PhysicalDevice& physicalDevice)
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

    ResultCode Device::BeginFrame()
    {
        if (IsInitialized() && !m_isInFrame)
        {
            m_isInFrame = true;
            return BeginFrameInternal();
        }
        return ResultCode::InvalidOperation;
    }

    ResultCode Device::EndFrame()
    {
        if (IsInitialized() && m_isInFrame)
        {
            EndFrameInternal();
            m_isInFrame = false;
            return ResultCode::Success;
        }
        return ResultCode::InvalidOperation;
    }

    ResultCode Device::WaitForIdle()
    {
        if (IsInitialized() && !m_isInFrame)
        {
            WaitForIdleInternal();
            return ResultCode::Success;
        }
        return ResultCode::InvalidOperation;
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
}