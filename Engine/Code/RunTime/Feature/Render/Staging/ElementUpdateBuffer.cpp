#include "ElementUpdateBuffer.h"

#include <Log/ILogSystem.h>
#include <RHI/Resource/Buffer/BufferBindFlags.h>

namespace Spark::Render
{
    bool ElementUpdateBuffer::Init(RHI::Device& device, const Descriptor& descriptor)
    {
        if (descriptor.m_elementSize == 0 || descriptor.m_elementSize % sizeof(uint32_t) != 0)
        {
            LOG_ERROR("[ElementUpdateBuffer] {}: element size {} is not a whole number of uints.",
                descriptor.m_name.GetCStr(), descriptor.m_elementSize);
            return false;
        }
        m_elementSize = descriptor.m_elementSize;

        StagingBuffer::Descriptor stagingDesc;
        stagingDesc.m_name      = descriptor.m_name;
        stagingDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead;
        return m_staging.Init(device, stagingDesc);
    }

    void ElementUpdateBuffer::Shutdown()
    {
        m_staging.Shutdown();
        m_capacity    = 0;
        m_updateCount = 0;
        m_indices     = nullptr;
        m_elements    = nullptr;
    }

    void ElementUpdateBuffer::Begin(uint32_t updateCount)
    {
        m_capacity       = 0;
        m_updateCount    = 0;
        m_indicesOffset  = 0;
        m_elementsOffset = 0;
        m_indices        = nullptr;
        m_elements       = nullptr;

        m_staging.Begin(uint64_t(updateCount) * (sizeof(uint32_t) + m_elementSize));
        if (updateCount == 0)
        {
            return;
        }

        const StagingBuffer::Allocation indices  = m_staging.Allocate(uint64_t(updateCount) * sizeof(uint32_t), sizeof(uint32_t));
        const StagingBuffer::Allocation elements = m_staging.Allocate(uint64_t(updateCount) * m_elementSize, sizeof(uint32_t));
        // None when the staging buffer could not grow: nothing is updated this frame.
        if (!indices.m_data || !elements.m_data)
        {
            return;
        }

        m_capacity       = updateCount;
        m_indicesOffset  = indices.m_offset;
        m_elementsOffset = elements.m_offset;
        m_indices        = static_cast<uint32_t*>(indices.m_data);
        m_elements       = static_cast<uint8_t*>(elements.m_data);
    }

    uint8_t* ElementUpdateBuffer::Append(uint32_t index)
    {
        if (m_updateCount >= m_capacity)
        {
            ASSERT(m_indices == nullptr, "[ElementUpdateBuffer] More elements set than the {} Begin was told.", m_capacity);
            return nullptr;
        }

        m_indices[m_updateCount] = index;
        return m_elements + uint64_t(m_updateCount++) * m_elementSize;
    }
}
