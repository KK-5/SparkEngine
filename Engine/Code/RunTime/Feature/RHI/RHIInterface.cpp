#include "RHIInterface.h"

#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    void RHIInterface::InitInternal()
    {
        RHIExecuteContext::Push(m_context);
        RHIEventExecuteContext::Push(m_eventContext);
    }

    void RHIInterface::ShutdownInternal()
    {
        m_eventContext.Clear();
        RHIEventExecuteContext::Pop();

        m_context.Clear();
        RHIExecuteContext::Pop();

        // After the components that may queue on their way out, before the backend shuts the
        // pools down: what is queued may hold resources of theirs.
        if (m_device)
        {
            m_device->FlushReleaseQueue();
        }
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

        LOG_INFO("[RHI] Physical Device: {}", physicalDevice.GetDescriptor().m_description.c_str());
        return result;
    }
}
