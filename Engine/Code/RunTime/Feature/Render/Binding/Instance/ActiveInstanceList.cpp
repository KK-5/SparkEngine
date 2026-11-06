#include "ActiveInstanceList.h"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <Log/ILogSystem.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/Buffer.h>

namespace Spark::Render
{
    bool ActiveInstanceList::Init(RHI::RHIContext& rhiCtx, RHI::Device& device,
        const ObjectName& name, RHI::BufferBindFlags bindFlags)
    {
        // The owner's update pass writes it on the Graphics queue.
        ReservedBuffer::Descriptor poolDesc;
        poolDesc.m_capacity  = ByteCount;
        poolDesc.m_pageSize  = device.GetLimits().m_reservedPageSizeInBytes;
        poolDesc.m_bindFlags = bindFlags;
        poolDesc.m_name      = name;
        if (!m_pool.Init(device, poolDesc))
        {
            LOG_ERROR("[ActiveInstanceList] {}: the ID buffer failed to init.", name.GetCStr());
            return false;
        }

        const eastl::string updateName(eastl::string::CtorSprintf(), "%s Update", name.GetCStr());
        ElementUpdateBuffer::Descriptor updateDesc;
        updateDesc.m_name        = ObjectName(updateName.c_str());
        updateDesc.m_elementSize = sizeof(uint32_t);
        if (!m_updates.Init(device, updateDesc))
        {
            LOG_ERROR("[ActiveInstanceList] {}: the ID updates failed to init.", name.GetCStr());
            m_pool.Shutdown();
            return false;
        }

        m_resource = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::Buffer>(m_resource, RHI::Components::Buffer{ Ptr<RHI::Buffer>(m_pool.GetBuffer()) });
        rhiCtx.Add<RHI::ImportedTag>(m_resource);
        rhiCtx.Add<RHI::ResourceName>(m_resource, RHI::ResourceName{ poolDesc.m_name });
        return true;
    }

    void ActiveInstanceList::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_resource != RHI::NullHandle)
        {
            rhiCtx.DestoryEntity(m_resource);
            m_resource = RHI::NullHandle;
        }
        m_updates.Shutdown();
        m_pool.Shutdown();
        m_mirror.clear();
    }

    uint32_t ActiveInstanceList::Update(eastl::span<const uint32_t> slots)
    {
        if (!m_pool.GetBuffer())
        {
            return 0;
        }

        const uint32_t count = MapPages(static_cast<uint32_t>(slots.size()));

        // A page mapped again has a backing of its own, without the entries it had; past the
        // list, what is left is read by no one.
        m_mirror.resize(eastl::min<size_t>(m_mirror.size(), count));

        m_changedPositions.clear();
        for (uint32_t position = 0; position < count; ++position)
        {
            if (position >= m_mirror.size() || m_mirror[position] != slots[position])
            {
                m_changedPositions.push_back(position);
            }
        }

        m_updates.Begin(static_cast<uint32_t>(m_changedPositions.size()));
        for (uint32_t position : m_changedPositions)
        {
            m_updates.Set(position, slots[position]);
        }

        // All or none are set: none only when the staging could not grow. Then this frame has
        // no list, and the next writes it whole.
        if (m_updates.GetUpdateCount() < m_changedPositions.size())
        {
            m_mirror.clear();
            return 0;
        }

        m_mirror.assign(slots.begin(), slots.begin() + count);
        return count;
    }

    uint32_t ActiveInstanceList::MapPages(uint32_t count)
    {
        const uint32_t entriesPerPage = static_cast<uint32_t>(m_pool.GetPageSize() / sizeof(uint32_t));
        const uint32_t neededPages    = (count + entriesPerPage - 1) / entriesPerPage;
        const uint32_t mappedPages    = m_pool.GetMappedPageCount();

        m_pageChanges.clear();
        for (uint32_t page = mappedPages; page < neededPages; ++page)
        {
            m_pageChanges.push_back(Suballocator::PageChange{ page, true });
        }
        for (uint32_t page = neededPages; page < mappedPages; ++page)
        {
            m_pageChanges.push_back(Suballocator::PageChange{ page, false });
        }
        m_pool.Update(m_pageChanges);

        // Short of them only when a page found no memory.
        return eastl::min(count, m_pool.GetMappedPageCount() * entriesPerPage);
    }
}
