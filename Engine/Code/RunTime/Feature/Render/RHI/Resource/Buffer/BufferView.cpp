#include "BufferView.h"

#include <Log/SpdLogSystem.h>

namespace Spark::Render::RHI
{
    ResultCode BufferView::Init(const Buffer& buffer, const BufferViewDescriptor& viewDescriptor)
    {
        if (!ValidateForInit(buffer, viewDescriptor))
        {
            return ResultCode::InvalidOperation;
        }

        if (Validation::isEnabled)
        {
            auto endOfView = (viewDescriptor.m_elementOffset + viewDescriptor.m_elementCount) * viewDescriptor.m_elementSize;
            if (buffer.GetDescriptor().m_byteCount < endOfView)
            {
                LOG_WARN(
                    "[DeviceBufferView]",
                    "Buffer view out of boundaries of buffer's memory. Buffer size {}. End of view {}",
                    buffer.GetDescriptor().m_byteCount,
                    (viewDescriptor.m_elementOffset + viewDescriptor.m_elementCount) * viewDescriptor.m_elementSize);
                return ResultCode::OutOfMemory;
            }
        }

        m_descriptor = viewDescriptor;
        return ResourceView::Init(buffer);
    }

    const BufferViewDescriptor& BufferView::GetDescriptor() const
    {
        return m_descriptor;
    }

    const Buffer& BufferView::GetBuffer() const
    {
        return static_cast<const Buffer&>(GetResource());
    }

    bool BufferView::IsFullView() const
    {
        const BufferDescriptor& bufferDescriptor = GetBuffer().GetDescriptor();
        const uint32_t bufferViewSize = m_descriptor.m_elementCount * m_descriptor.m_elementSize;
        return m_descriptor.m_elementOffset == 0 && bufferViewSize >= bufferDescriptor.m_byteCount;
    }

    bool BufferView::ValidateForInit(const Buffer& buffer, const BufferViewDescriptor& viewDescriptor) const
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_WARN("[DeviceBufferView] Buffer view already initialized");
                return false;
            }

            if (!buffer.IsInitialized())
            {
                LOG_WARN("[DeviceBufferView] Attempting to create view from uninitialized buffer {}.", buffer.GetName().GetCStr());
                return false;
            }

            if (!CheckBitsAll(buffer.GetDescriptor().m_bindFlags, viewDescriptor.m_overrideBindFlags))
            {
                LOG_WARN("[DeviceBufferView] Buffer view has bind flags that are incompatible with the underlying buffer.");
            
                return false;
            }
        }

        return true;
    }
}