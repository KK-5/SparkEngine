#include "CommandRecorder.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Base.h>

namespace Spark::RHI
{
    RHI::ResultCode CommandRecorder::Init(Device& device, const CommandRecorderDescriptor& desc)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                return RHI::ResultCode::InvalidOperation;
            }
        }

        RHI::ResultCode result = InitInternal(device, desc);

        if (result == RHI::ResultCode::Success)
        {
            m_desc = desc;
            DeviceObject::Init(device);
        }
        return result;
    }

    void CommandRecorder::Shutdown()
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[CommandRecorder] Shutdown an uninitialize command recorder");
                return;
            }
        }

        ShutdownInternal();
        DeviceObject::Shutdown();
    }

    void CommandRecorder::Reset()
    {
        ASSERT(IsInitialized(), "[CommandRecorder] Reset an uninitialized command recorder");
        ASSERT(m_commandList, "[CommandRecorder] Command list is null");
        ResetInternal();
    }

    CommandList* CommandRecorder::GetCommandList()
    {
        return m_commandList;
    }

    const CommandList* CommandRecorder::GetCommandList() const
    {
        return m_commandList;
    }

    const CommandRecorderDescriptor& CommandRecorder::GetDesc() const
    {
        return m_desc;
    }
}