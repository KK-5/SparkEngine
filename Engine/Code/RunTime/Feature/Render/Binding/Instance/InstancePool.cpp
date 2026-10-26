#include "InstancePool.h"

#include <string.h>

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Shader/ShaderBindingsUtils.h>

namespace Spark::Render
{
    bool InstancePool::Init(RHI::RHIContext& rhiCtx, RHI::Device& device, RHI::RHIHandle bindings)
    {
        const uint64_t pageSize = device.GetLimits().m_reservedPageSizeInBytes;

        // Read by shaders on the Graphics queue, where its owner's pass writes it.
        ReservedBuffer::Descriptor poolDesc;
        poolDesc.m_capacity  = ByteCount;
        poolDesc.m_pageSize  = pageSize;
        poolDesc.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite;
        poolDesc.m_name      = ObjectName("g_Instances");
        if (!m_pool.Init(device, poolDesc))
        {
            LOG_ERROR("[InstancePool] The instance buffer failed to init; no instance will be drawn.");
            return false;
        }

        ElementUpdateBuffer::Descriptor updateDesc;
        updateDesc.m_name        = ObjectName("InstanceUpdate");
        updateDesc.m_elementSize = sizeof(InstanceData);
        if (!m_updates.Init(device, updateDesc))
        {
            LOG_ERROR("[InstancePool] The instance updates failed to init; no instance will be drawn.");
            m_pool.Shutdown();
            return false;
        }

        // A slot is a record: its offset is a whole number of them.
        m_allocator = new Suballocator(device, ByteCount, pageSize);

        m_resource = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::Buffer>(m_resource, RHI::Components::Buffer{ Ptr<RHI::Buffer>(m_pool.GetBuffer()) });
        rhiCtx.Add<RHI::ImportedTag>(m_resource);
        rhiCtx.Add<RHI::ResourceName>(m_resource, RHI::ResourceName{ poolDesc.m_name });
        rhiCtx.Add<InstancePoolTag>(m_resource);

        const RHI::BufferViewDescriptor viewDesc =
            RHI::BufferViewDescriptor::CreateStructured(0, InstanceCapacity, sizeof(InstanceData));
        if (RHI::BufferView* view = RHI::GetOrCreateBufferView(rhiCtx, m_resource, *m_pool.GetBuffer(), viewDesc))
        {
            SetShaderBuffer(bindings, RHI::InputName("g_Instances"), view);
            m_bindings = bindings;
        }

        m_records.resize(InstanceCapacity);
        m_known.assign(InstanceCapacity, 0);
        return true;
    }

    void InstancePool::Shutdown(RHI::RHIContext& rhiCtx)
    {
        // The view holds the buffer, which goes before its pool.
        if (m_bindings != RHI::NullHandle)
        {
            SetShaderBuffer(m_bindings, RHI::InputName("g_Instances"), nullptr);
            m_bindings = RHI::NullHandle;
        }
        if (m_resource != RHI::NullHandle)
        {
            rhiCtx.DestoryEntity(m_resource);
            m_resource = RHI::NullHandle;
        }
        m_updates.Shutdown();
        m_pool.Shutdown();
        // The slots, all gone, hold it until the device hands them back.
        m_allocator.reset();
        m_records.clear();
        m_known.clear();
    }

    Suballocation InstancePool::Allocate()
    {
        return m_allocator ? m_allocator->Allocate(sizeof(InstanceData)) : Suballocation{};
    }

    void InstancePool::BeginUpdate()
    {
        if (!m_allocator)
        {
            return;
        }

        m_allocator->TakePageChanges(m_pageChanges);
        m_pool.Update(m_pageChanges);

        // A page mapped again has a backing of its own, without the records it had.
        const uint64_t pageSize = m_pool.GetPageSize();
        for (const Suballocator::PageChange& change : m_pageChanges)
        {
            if (!change.m_occupied)
            {
                continue;
            }
            const uint64_t first = change.m_page * pageSize / sizeof(InstanceData);
            const uint64_t last  = eastl::min<uint64_t>(((change.m_page + 1) * pageSize - 1) / sizeof(InstanceData), InstanceCapacity - 1);
            eastl::fill(m_known.begin() + first, m_known.begin() + last + 1, uint8_t(0));
        }
    }

    void InstancePool::Set(uint32_t slot, const InstanceData& record)
    {
        if (m_known[slot] && memcmp(&m_records[slot], &record, sizeof(InstanceData)) == 0)
        {
            return;
        }
        m_records[slot] = record;
        m_known[slot]   = 1;
        m_changedSlots.push_back(slot);
    }

    void InstancePool::EndUpdate()
    {
        if (!m_allocator)
        {
            return;
        }

        m_updates.Begin(static_cast<uint32_t>(m_changedSlots.size()));
        for (uint32_t slot : m_changedSlots)
        {
            m_updates.Set(slot, m_records[slot]);
        }

        // All or none are set: none only when the staging could not grow. Then the GPU does not
        // hold them, and the next frame writes them.
        if (m_updates.GetUpdateCount() < m_changedSlots.size())
        {
            for (uint32_t slot : m_changedSlots)
            {
                m_known[slot] = 0;
            }
        }
        m_changedSlots.clear();
    }
}
