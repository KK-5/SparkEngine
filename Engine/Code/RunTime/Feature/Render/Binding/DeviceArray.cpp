#include "DeviceArray.h"

#include <string.h>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Shader/ShaderBindingsUtils.h>

namespace Spark::Render
{
    bool DeviceArray::Init(RHI::RHIContext& rhiCtx, RHI::Device& device, const Descriptor& descriptor)
    {
        m_descriptor = descriptor;
        const uint64_t pageSize = device.GetLimits().m_reservedPageSizeInBytes;

        // Read by shaders on the Graphics queue, where its owner's pass writes it.
        ReservedBuffer::Descriptor poolDesc;
        poolDesc.m_capacity  = GetByteCount();
        poolDesc.m_pageSize  = pageSize;
        poolDesc.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite;
        poolDesc.m_name      = descriptor.m_name;
        if (!m_pool.Init(device, poolDesc))
        {
            LOG_ERROR("[DeviceArray] {}: the buffer failed to init.", descriptor.m_name.GetCStr());
            return false;
        }

        const eastl::string updateName(eastl::string::CtorSprintf(), "%s Update", descriptor.m_name.GetCStr());
        ElementUpdateBuffer::Descriptor updateDesc;
        updateDesc.m_name        = ObjectName(updateName.c_str());
        updateDesc.m_elementSize = descriptor.m_elementSize;
        if (!m_updates.Init(device, updateDesc))
        {
            LOG_ERROR("[DeviceArray] {}: the updates failed to init.", descriptor.m_name.GetCStr());
            m_pool.Shutdown();
            return false;
        }

        m_allocator = new Suballocator(device, GetByteCount(), pageSize);

        m_resource = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::Buffer>(m_resource, RHI::Components::Buffer{ Ptr<RHI::Buffer>(m_pool.GetBuffer()) });
        rhiCtx.Add<RHI::ImportedTag>(m_resource);
        rhiCtx.Add<RHI::ResourceName>(m_resource, RHI::ResourceName{ descriptor.m_name });

        const RHI::BufferViewDescriptor viewDesc = RHI::BufferViewDescriptor::CreateStructured(
            0, descriptor.m_capacity, descriptor.m_elementSize);
        if (RHI::BufferView* view = RHI::GetOrCreateBufferView(rhiCtx, m_resource, *m_pool.GetBuffer(), viewDesc))
        {
            SetShaderBuffer(descriptor.m_bindings, descriptor.m_inputName, view);
        }

        return true;
    }

    void DeviceArray::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (!m_allocator)
        {
            return;
        }

        // The view holds the buffer, which goes before its pool.
        SetShaderBuffer(m_descriptor.m_bindings, m_descriptor.m_inputName, nullptr);
        rhiCtx.DestoryEntity(m_resource);
        m_resource = RHI::NullHandle;
        m_updates.Shutdown();
        m_pool.Shutdown();
        // The elements, all gone, hold it until the device hands them back.
        m_allocator.reset();
        m_elements.clear();
        m_known.clear();
    }

    Suballocation DeviceArray::Allocate()
    {
        return m_allocator ? m_allocator->Allocate(m_descriptor.m_elementSize) : Suballocation{};
    }

    void DeviceArray::BeginUpdate()
    {
        if (!m_allocator)
        {
            return;
        }

        m_allocator->TakePageChanges(m_pageChanges);
        m_pool.Update(m_pageChanges);

        // A page mapped again has a backing of its own, without the elements it had. Past the
        // mirror nothing is known anyway.
        const uint64_t pageSize    = m_pool.GetPageSize();
        const uint64_t elementSize = m_descriptor.m_elementSize;
        for (const Suballocator::PageChange& change : m_pageChanges)
        {
            const uint64_t first = change.m_page * pageSize / elementSize;
            if (!change.m_occupied || first >= m_known.size())
            {
                continue;
            }
            const uint64_t end = eastl::min<uint64_t>(((change.m_page + 1) * pageSize - 1) / elementSize + 1,
                m_known.size());
            eastl::fill(m_known.begin() + first, m_known.begin() + end, uint8_t(0));
        }
    }

    void DeviceArray::SetBytes(uint32_t index, const void* bytes)
    {
        // The mirror reaches the highest element set, not the capacity, which only bounds the
        // address space.
        if (index >= m_known.size())
        {
            m_known.resize(index + 1, uint8_t(0));
            m_elements.resize(uint64_t(index + 1) * m_descriptor.m_elementSize);
        }

        uint8_t* element = m_elements.data() + uint64_t(index) * m_descriptor.m_elementSize;
        if (m_known[index] && memcmp(element, bytes, m_descriptor.m_elementSize) == 0)
        {
            return;
        }
        memcpy(element, bytes, m_descriptor.m_elementSize);
        m_known[index] = 1;
        m_changed.push_back(index);
    }

    void DeviceArray::EndUpdate()
    {
        if (!m_allocator)
        {
            return;
        }

        m_updates.Begin(static_cast<uint32_t>(m_changed.size()));
        for (uint32_t index : m_changed)
        {
            m_updates.SetBytes(index, m_elements.data() + uint64_t(index) * m_descriptor.m_elementSize);
        }

        // All or none are set: none only when the staging could not grow. Then the GPU does not
        // hold them, and the next frame writes them.
        if (m_updates.GetUpdateCount() < m_changed.size())
        {
            for (uint32_t index : m_changed)
            {
                m_known[index] = 0;
            }
        }
        m_changed.clear();
    }
}
