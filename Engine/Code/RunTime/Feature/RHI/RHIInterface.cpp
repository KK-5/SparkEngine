#include "RHIInterface.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    void RHIInterface::InitInternal()
    {
        RHIExecuteContext::Push(m_context);
    }

    void RHIInterface::ShutdownInternal()
    {
        // Device::Shutdown is private — release via Ptr<> destructor which
        // routes through DeviceObject's RAII chain.
        m_device.reset();
        RHIExecuteContext::Pop();
    }

    PhysicalDeviceList RHIInterface::EnumeratePhysicalDevices() const
    {
        auto* factory = GetRHIFactory();
        ASSERT(factory != nullptr,
            "[RHIInterface] EnumeratePhysicalDevices called before backend init.");
        return factory->EnumeratePhysicalDevices();
    }

    ResultCode RHIInterface::InitDevice(PhysicalDevice& physicalDevice, const DeviceDescriptor& descriptor)
    {
        ASSERT(m_device == nullptr,
            "[RHIInterface] InitDevice called twice — Device is engine-wide and may only be initialized once.");

        auto* factory = GetRHIFactory();
        ASSERT(factory != nullptr,
            "[RHIInterface] InitDevice called before backend init.");

        m_device = factory->CreateDevice();
        if (!m_device)
        {
            LOG_ERROR("[RHIInterface] Factory::CreateDevice returned null.");
            return ResultCode::Fail;
        }

        const ResultCode result = m_device->Init(physicalDevice, descriptor);
        if (result != ResultCode::Success)
        {
            LOG_ERROR("[RHIInterface] Device::Init failed.");
            m_device.reset();
        }
        return result;
    }
}
