#include "StagingBuffer.h"

#include <EASTL/string.h>

#include <Log/ILogSystem.h>
#include <Math/Bit.h>
#include <Service/Service.h>
#include <RHI/Factory.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferPool.h>

namespace Spark::Render
{
    StagingBuffer::~StagingBuffer()
    {
        ASSERT(m_slots.empty(), "[StagingBuffer] {} destroyed before Shutdown.", m_descriptor.m_name.GetCStr());
    }

    bool StagingBuffer::Init(RHI::Device& device, const Descriptor& descriptor)
    {
        m_device     = &device;
        m_descriptor = descriptor;

        RHI::BufferPoolDescriptor poolDescriptor;
        poolDescriptor.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
        poolDescriptor.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
        poolDescriptor.m_bindFlags        = descriptor.m_bindFlags;

        m_pool = Service<RHI::Factory>::Get()->CreateBufferPool();
        m_pool->SetName(descriptor.m_name);
        if (m_pool->Init(device, poolDescriptor) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[StagingBuffer] {}: buffer pool init failed.", descriptor.m_name.GetCStr());
            m_pool.reset();
            return false;
        }

        m_slots.resize(device.GetDescriptor().m_frameCountMax);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i)
        {
            if (!CreateSlot(m_slots[i], i, eastl::max<uint64_t>(descriptor.m_initialCapacity, 1)))
            {
                Shutdown();
                return false;
            }
        }
        return true;
    }

    void StagingBuffer::Shutdown()
    {
        for (Slot& slot : m_slots)
        {
            DestroySlot(slot);
        }
        m_slots.clear();
        m_pool.reset();
    }

    void StagingBuffer::Begin(uint64_t byteCount)
    {
        m_current       = m_device->GetFrameIndex();
        m_reservedBytes = byteCount;
        m_usedBytes     = 0;

        Slot& slot = m_slots[m_current];
        // None when a replacement failed.
        const uint64_t capacity = slot.m_buffer ? slot.m_buffer->GetDescriptor().m_byteCount : 0;
        if (byteCount <= capacity)
        {
            return;
        }

        // The GPU last read it frameCountMax frames ago; the pool holds its memory past that.
        DestroySlot(slot);
        if (!CreateSlot(slot, m_current, eastl::max(byteCount, capacity * 2)))
        {
            m_reservedBytes = 0;
        }
    }

    StagingBuffer::Allocation StagingBuffer::Allocate(uint64_t byteCount, uint64_t alignment)
    {
        const uint64_t offset = AlignUpNPOT(m_usedBytes, alignment);
        if (offset + byteCount > m_reservedBytes)
        {
            ASSERT(false, "[StagingBuffer] {}: {} bytes at {} run past the {} Begin was told.",
                m_descriptor.m_name.GetCStr(), byteCount, offset, m_reservedBytes);
            return {};
        }

        m_usedBytes = offset + byteCount;
        return Allocation{ offset, m_slots[m_current].m_data + offset };
    }

    RHI::RHIHandle StagingBuffer::GetResource() const
    {
        return m_slots.empty() ? RHI::NullHandle : m_slots[m_current].m_resource;
    }

    RHI::Buffer* StagingBuffer::GetBuffer() const
    {
        return m_slots.empty() ? nullptr : m_slots[m_current].m_buffer.get();
    }

    bool StagingBuffer::CreateSlot(Slot& slot, uint32_t index, uint64_t capacity)
    {
        const eastl::string name(eastl::string::CtorSprintf(), "%s %u", m_descriptor.m_name.GetCStr(), index);

        Ptr<RHI::Buffer> buffer = Service<RHI::Factory>::Get()->CreateBuffer();
        buffer->SetName(ObjectName{ name.c_str() });
        RHI::BufferInitRequest request;
        request.m_buffer     = buffer.get();
        request.m_descriptor = RHI::BufferDescriptor(m_descriptor.m_bindFlags, capacity);
        if (m_pool->InitBuffer(request) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[StagingBuffer] {}: no memory for {} bytes.", name.c_str(), capacity);
            return false;
        }

        RHI::BufferMapResponse response;
        if (m_pool->MapBuffer(RHI::BufferMapRequest(*buffer, 0, capacity), response) != RHI::ResultCode::Success
            || !response.m_data)
        {
            LOG_ERROR("[StagingBuffer] {}: mapping failed.", name.c_str());
            return false;
        }

        auto& context  = *RHI::RHIExecuteContext::Current();
        slot.m_buffer   = buffer;
        slot.m_data     = static_cast<uint8_t*>(response.m_data);
        slot.m_resource = context.CreateEntity();
        context.Add<RHI::Components::Buffer>(slot.m_resource, RHI::Components::Buffer{ eastl::move(buffer) });
        context.Add<RHI::ImportedTag>(slot.m_resource);
        context.Add<RHI::ResourceName>(slot.m_resource, RHI::ResourceName{ ObjectName{ name.c_str() } });
        return true;
    }

    void StagingBuffer::DestroySlot(Slot& slot)
    {
        if (slot.m_data)
        {
            m_pool->UnmapBuffer(*slot.m_buffer);
            slot.m_data = nullptr;
        }
        if (slot.m_resource != RHI::NullHandle)
        {
            RHI::RHIExecuteContext::Current()->DestoryEntity(slot.m_resource);
            slot.m_resource = RHI::NullHandle;
        }
        slot.m_buffer.reset();
    }
}
