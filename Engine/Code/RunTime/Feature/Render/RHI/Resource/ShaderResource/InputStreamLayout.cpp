#include "InputStreamLayout.h"

#include <Log/SpdLogSystem.h>

#include <Base.h>

namespace Spark::RHI
{
    StreamChannelDescriptor::StreamChannelDescriptor(
        ShaderSemantic semantic,
        Format format,
        uint32_t byteOffset,
        uint32_t bufferIndex)
        : m_semantic{eastl::move(semantic)}
        , m_format{format}
        , m_byteOffset{byteOffset}
        , m_bufferIndex{bufferIndex}
    {}

    size_t StreamChannelDescriptor::GetHash() const
    {
        size_t h1 = eastl::hash<uint32_t>()(static_cast<uint32_t>(m_format));
        size_t h2 = eastl::hash<uint32_t>()(m_byteOffset);
        size_t h3 = eastl::hash<uint32_t>()(m_bufferIndex);
        size_t h4 = m_semantic.GetHash();

        size_t combinedHash = h1;
        combinedHash ^= h2 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        combinedHash ^= h3 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        combinedHash ^= h4 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        return combinedHash;
    }

    StreamBufferDescriptor::StreamBufferDescriptor(
        StreamStepFunction stepFunction,
        uint32_t stepRate,
        uint32_t byteStride)
        : m_stepFunction{stepFunction}
        , m_stepRate{stepRate}
        , m_byteStride{byteStride}
    {}

    size_t StreamBufferDescriptor::GetHash() const
    {
        size_t h1 = eastl::hash<uint32_t>()(static_cast<uint32_t>(m_stepFunction));
        size_t h2 = eastl::hash<uint32_t>()(m_stepRate);
        size_t h3 = eastl::hash<uint32_t>()(m_byteStride);

        size_t combinedHash = h1;
        combinedHash ^= h2 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        combinedHash ^= h3 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        return combinedHash;
    }

    void InputStreamLayout::Clear()
    {
        m_topology = PrimitiveTopology::Undefined;
        m_streamChannels.clear();
        m_streamBuffers.clear();
        m_hash = 0 ;
    }

    bool InputStreamLayout::Finalize()
    {
        if (Validation::isEnabled)
        {
            for (const auto& channelDescriptor : m_streamChannels)
            {
                if (channelDescriptor.m_bufferIndex >= m_streamBuffers.size())
                {
                    LOG_ERROR("[InputStreamLayout] Channel references buffer index {} which does not exist.",
                        channelDescriptor.m_bufferIndex);
                    return false;
                }
            }

            if (m_topology == PrimitiveTopology::Undefined)
            {
                LOG_ERROR("[InputStreamLayout] Topology is undefined.");
                return false;
            }
        }

        size_t seed = eastl::hash<uint32_t>()(static_cast<uint32_t>(m_topology));

        for (const StreamChannelDescriptor& channel : m_streamChannels)
        {
            size_t hash = channel.GetHash();
            seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        for (const StreamBufferDescriptor& buffer : m_streamBuffers)
        {
            size_t hash = buffer.GetHash();
            seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        m_hash = seed;

        return true;
    }

    bool InputStreamLayout::IsFinalized() const
    {
        return m_hash != 0 ;
    }

    void InputStreamLayout::SetTopology(PrimitiveTopology topology)
    {
        m_topology = topology;
    }

    void InputStreamLayout::AddStreamChannel(const StreamChannelDescriptor& descriptor)
    {
        m_streamChannels.push_back(descriptor);
    }

    void InputStreamLayout::AddStreamBuffer(const StreamBufferDescriptor& descriptor)
    {
        m_streamBuffers.push_back(descriptor);
    }

    const PrimitiveTopology InputStreamLayout::GetTopology() const
    {
        return m_topology;
    }

    eastl::span<const StreamChannelDescriptor> InputStreamLayout::GetStreamChannels() const
    {
        return m_streamChannels;
    }

    eastl::span<const StreamBufferDescriptor> InputStreamLayout::GetStreamBuffers() const
    {
        return m_streamBuffers;
    }

    uint32_t InputStreamLayout::GetHash() const
    {
        return m_hash;
    }

    bool InputStreamLayout::operator == (const InputStreamLayout& rhs) const
    {
        bool same = (m_streamChannels.size() == rhs.m_streamChannels.size() && m_streamBuffers.size() == rhs.m_streamBuffers.size());
        if (same)
        {
            for (size_t index = 0; index < m_streamChannels.size(); index++)
            {
                if (m_streamChannels[index].GetHash() != rhs.m_streamChannels[index].GetHash())
                {
                    same = false;
                    break;
                }
            }

            if (same)
            {
                for (size_t index = 0; index < m_streamBuffers.size(); index++)
                {
                    if (m_streamBuffers[index].GetHash() != rhs.m_streamBuffers[index].GetHash())
                    {
                        same = false;
                        break;
                    }
                }
            }
        }

        return same && m_hash == rhs.m_hash && m_topology == rhs.m_topology;
    }
}