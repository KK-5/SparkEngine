/*
 * Modified by SparkEngine in 2025
 *  -- MapBuffer and InitBuffer reject Device heap buffers, no auto staging buffer.
 */

#include "BufferPool.h"

#include <Log/ILogSystem.h>

namespace Spark::RHI
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

        // Set initial resource state using Vulkan-style (Usage, Access) model.
        SetResourceState(*request.m_buffer, RHI::ResourceState{});

        ResultCode resultCode = ResourcePool::InitResource(request.m_buffer, [this, &request](){
            return InitBufferInternal(*request.m_buffer, request.m_descriptor);
        });

        if (resultCode == ResultCode::Success && request.m_initialData)
        {
            if (m_descriptor.m_heapMemoryLevel == HeapMemoryLevel::Device)
            {
                LOG_ERROR("[BufferPool] Initial data upload for Device heap buffers "
                          "is not supported via Map. Use a Host heap staging buffer and copy command instead.");
                return ResultCode::InvalidOperation;
            }

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

        if (!ValidateIsRegistered(&buffer))
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

        if (!ValidateIsRegistered(request.m_buffer))
        {
            return ResultCode::InvalidArgument;
        }

        if (!ValidateMapRequest(request))
        {
            return ResultCode::InvalidArgument;
        }

        if (m_descriptor.m_heapMemoryLevel == HeapMemoryLevel::Device)
        {
            LOG_ERROR("[BufferPool] Cannot map buffer {} from a Device heap pool. "
                      "Use a Host heap pool or StreamBuffer for uploads.",
                      request.m_buffer->GetName().GetCStr());
            return ResultCode::InvalidOperation;
        }

        ResultCode resultCode = MapBufferInternal(request, response);
        ValidateBufferMap(*request.m_buffer, response.m_data != nullptr);
        return resultCode;
    }

    void BufferPool::UnmapBuffer(Buffer& buffer)
    {
        if (ValidateIsInitialized() && ValidateNotProcessingFrame() && ValidateIsRegistered(&buffer) && ValidateBufferUnmap(buffer))
        {
            UnmapBufferInternal(buffer);
        }
    }

    void BufferPool::BufferCopy(void* destination, const void* source, size_t num)
    {
        memcpy(destination, source, num);
    }

    void BufferPool::MemcpySubresource(MemoryCopyDest* dest, MemoryCopySrc* src, size_t rowSizeInBytes, uint32_t numRows, uint32_t numSlices)
    {
        for (uint32_t z = 0; z < numSlices; ++z)
        {
            uint8_t* pDestSlice = reinterpret_cast<uint8_t*>(dest->pData) + dest->slicePitch * z;
            const uint8_t* pSrcSlice = reinterpret_cast<const uint8_t*>(src->pData) + src->slicePitch * z;
            for (uint32_t y = 0; y < numRows; ++y)
            {
                memcpy(pDestSlice + dest->rowPitch * y,
                    pSrcSlice + src->rowPitch * y,
                    rowSizeInBytes);
            }
        }
    }

    const BufferPoolDescriptor& BufferPool::GetDescriptor() const
    {
        return m_descriptor;
    }

    void BufferPool::OnFrameBegin()
    {
        if (Validation::isEnabled)
        {
            if (GetMapRefCount() != 0 && GetDescriptor().m_heapMemoryLevel == HeapMemoryLevel::Device)
            {
                LOG_ERROR("[BufferPool] There are currently buffers mapped on buffer pool {}"
                "All buffers must be unmapped when the frame is processing.", GetName().GetCStr() ? GetName().GetCStr() : "[Unknow]");
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

            // Pool bind flags must contain all buffer bind flags.
            if ((poolDescriptor.m_bindFlags & initRequest.m_descriptor.m_bindFlags) != initRequest.m_descriptor.m_bindFlags)
            {
                LOG_ERROR("[BufferPool] Pool bind flags do not contain buffer bind flags in pool {}", GetName().GetCStr());
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