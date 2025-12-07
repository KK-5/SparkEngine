#include "BufferPool.h"

#include <Log/SpdLogSystem.h>

namespace Spark::Render::RHI
{
    ResultCode BufferPool::Init(Device& device, const BufferPoolDescriptor& descriptor)
    {
        return ResourcePool::Init(
            device, descriptor,
            [this, &device, &descriptor]()
        {
            if (!ValidatePoolDescriptor(descriptor))
            {
                return ResultCode::InvalidArgument;
            }

            m_descriptor = descriptor;

            return InitInternal(device, descriptor);
        });
    }

    ResultCode BufferPool::InitBuffer(const BufferInitRequest& request)
    {
        if (!ValidateInitRequest(request))
        {
            return ResultCode::InvalidArgument;
        }

        request.m_buffer->SetDescriptor(request.m_descriptor);

        ResultCode resultCode = ResourcePool::InitResource(request.m_buffer, [this, &request](){
            return InitBufferInternal(*request.m_buffer, request.m_descriptor);
        });

        if (resultCode == ResultCode::Success && request.m_initialData)
        {
            BufferMapRequest mapRequest;
            mapRequest.m_buffer = request.m_buffer;
            mapRequest.m_byteCount = request.m_descriptor.m_byteCount;
            mapRequest.m_byteOffset = 0;

            BufferMapResponse mapResponse;
            resultCode = MapBufferInternal(mapRequest, mapResponse);
            if (resultCode == ResultCode::Success)
            {
                BufferCopy(mapResponse.m_data, request.m_initialData, request.m_descriptor.m_byteCount);
                UnmapBufferInternal(*request.m_buffer);
            }
        }

        return resultCode;
    }

    ResultCode BufferPool::OrphanBuffer(Buffer& buffer)
    {
        if (!ValidateIsInitialized() || !ValidateIsHostHeap() || !ValidateNotProcessingFrame())
        {
            return ResultCode::InvalidOperation;
        }

        if (!IsRegistered(&buffer))
        {
            return ResultCode::InvalidArgument;
        }
            
        return OrphanBufferInternal(buffer);
    }

    ResultCode BufferPool::MapBuffer(const BufferMapRequest& request, BufferMapResponse& response)
    {
        if (!ValidateIsInitialized() || !ValidateNotProcessingFrame())
        {
            return ResultCode::InvalidOperation;
        }

        if (!IsRegistered(request.m_buffer))
        {
            return ResultCode::InvalidArgument;
        }

        if (!ValidateMapRequest(request))
        {
            return ResultCode::InvalidArgument;
        }

        ResultCode resultCode = MapBufferInternal(request, response);
        ValidateBufferMap(*request.m_buffer, response.m_data != nullptr);
        return resultCode;
    }

    void BufferPool::UnmapBuffer(Buffer& buffer)
    {
        if (ValidateIsInitialized() && ValidateNotProcessingFrame() && IsRegistered(&buffer) && ValidateBufferUnmap(buffer))
        {
            UnmapBufferInternal(buffer);
        }
    }

    ResultCode BufferPool::StreamBuffer(const BufferStreamRequest& request)
    {
        if (!ValidateIsInitialized())
        {
            return ResultCode::InvalidOperation;
        }

        if (!IsRegistered(request.m_buffer))
        {
            return ResultCode::InvalidArgument;
        }

        return StreamBufferInternal(request);
    }

    const BufferPoolDescriptor& BufferPool::GetDescriptor() const
    {
        return m_descriptor;
    }

    void BufferPool::OnFrameBegin()
    {
        if (Validation::isEnabled)
        {
            if (GetMapRefCount() == 0 || GetDescriptor().m_heapMemoryLevel != HeapMemoryLevel::Device)
            {
                LOG_ERROR("[BufferPool] There are currently buffers mapped on buffer pool {}"
                "All buffers must be unmapped when the frame is processing.", GetName().GetCStr());
            }
        }

        ResourcePool::OnFrameBegin();
    }

    uint32_t BufferPool::GetMapRefCount() const
    {
        return m_mapRefCount;
    }

    void BufferPool::ValidateBufferMap(Buffer& buffer, bool isDataValid)
    {
        if (Validation::isEnabled)
        {
            if (!isDataValid)
            {
                LOG_ERROR("[BufferPool] Failed to map buffer {}.", buffer.GetName().GetCStr());
            }
            ++buffer.m_mapRefCount;
            ++m_mapRefCount;
        }
    }


    bool BufferPool::ValidateBufferUnmap(Buffer& buffer)
    {
        if (Validation::isEnabled)
        {
            if (--buffer.m_mapRefCount == -1)
            {
                LOG_ERROR("[BufferPool] DeviceBuffer {} was unmapped more times than it was mapped.", buffer.GetName().GetCStr());

                // Undo the ref-count to keep the validation state sane.
                ++buffer.m_mapRefCount;
                return false;
            }
            else
            {
                --m_mapRefCount;
            }
        }
        return true;
    }

    bool BufferPool::ValidatePoolDescriptor(const BufferPoolDescriptor& descriptor) const
    {
        if (Validation::isEnabled)
        {
            if (descriptor.m_heapMemoryLevel == RHI::HeapMemoryLevel::Device &&
                descriptor.m_hostMemoryAccess == RHI::HostMemoryAccess::Read)
            {
                LOG_ERROR("[BufferPool] When HeapMemoryLevel::Device is specified, m_hostMemoryAccess must be HostMemoryAccess::Write.");
                return false;
            }
        }
        
        return true;
    }

    bool BufferPool::ValidateInitRequest(const BufferInitRequest& initRequest) const
    {
        if (Validation::isEnabled)
        {
            const BufferPoolDescriptor& poolDescriptor = GetDescriptor();

            // Bind flags of the buffer must match the pool bind flags.
            if (initRequest.m_descriptor.m_bindFlags != poolDescriptor.m_bindFlags)
            {
                LOG_ERROR("[BufferPool] DeviceBuffer bind flags don't match pool bind flags in pool {}", GetName().GetCStr());
                return false;
            }

            // Initial data is not allowed for read-only heaps.
            if (initRequest.m_initialData && poolDescriptor.m_hostMemoryAccess == HostMemoryAccess::Read)
            {
                LOG_ERROR("[BufferPool] Initial data is not allowed with read-only pools.");
                return false;
            }
        }

        return true;
    }

    bool BufferPool::ValidateIsHostHeap() const
    {
        if (Validation::isEnabled)
        {
            if (GetDescriptor().m_heapMemoryLevel != HeapMemoryLevel::Host)
            {
                LOG_ERROR("[BufferPool] This operation is only permitted for pools on the Host heap.");
                return false;
            }
        }
        return true;
    }

    bool BufferPool::ValidateMapRequest(const BufferMapRequest& request) const
    {
        if (Validation::isEnabled)
        {
            if (!request.m_buffer)
            {
                LOG_ERROR("[BufferPool] Trying to map a null buffer {}.", request.m_buffer->GetName().GetCStr());
                return false;
            }

            if (request.m_byteCount == 0)
            {
                LOG_ERROR("[BufferPool] Trying to map zero bytes from buffer {}.", request.m_buffer->GetName().GetCStr());
                return false;
            }

            if (request.m_byteOffset + request.m_byteCount > request.m_buffer->GetDescriptor().m_byteCount)
            {
                LOG_ERROR(
                    "[BufferPool] Unable to map buffer {}, overrunning the size of the buffer.",
                    request.m_buffer->GetName().GetCStr());
                return false;
            }
        }
        return true;
    }
}