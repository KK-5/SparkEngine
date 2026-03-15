#include "InputStreamLayoutBuilder.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    InputStreamLayoutBuilder::InputStreamLayoutBuilder()
    {
        Begin();
    }

    void InputStreamLayoutBuilder::Begin()
    {
        m_topology = PrimitiveTopology::TriangleList;
        m_bufferDescriptorBuilders.clear();
    }

    InputStreamLayout InputStreamLayoutBuilder::End()
    {
        InputStreamLayout layout;

        layout.SetTopology(m_topology);

        for (BufferDescriptorBuilder& builder : m_bufferDescriptorBuilders)
        {
            builder.m_bufferDescriptor.m_byteStride = builder.m_byteOffset;
            layout.AddStreamBuffer(builder.m_bufferDescriptor);

            for (StreamChannelDescriptor& channelDescriptor : builder.m_channelDescriptors)
            {
                layout.AddStreamChannel(channelDescriptor);
            }
        }

        layout.Finalize();

        return layout;
    }

    void InputStreamLayoutBuilder::SetTopology(PrimitiveTopology topology)
    {
        m_topology = topology;
    }

    InputStreamLayoutBuilder::BufferDescriptorBuilder* InputStreamLayoutBuilder::BufferDescriptorBuilder::Channel(const ShaderSemantic& semantic, Format format)
    {
        if (m_channelDescriptors.size() == m_channelDescriptors.capacity())
        {
            LOG_ERROR("[InputStreamLayoutBuilder] No space to add stream channel.");
        }
        else
        {
            StreamChannelDescriptor& channel = m_channelDescriptors.emplace_back();

            channel.m_bufferIndex = m_bufferIndex;
            channel.m_byteOffset = m_byteOffset;
            channel.m_format = format;
            channel.m_semantic = semantic;

            m_byteOffset += GetFormatSize(format);
        }

        return this;
    }

    InputStreamLayoutBuilder::BufferDescriptorBuilder* InputStreamLayoutBuilder::BufferDescriptorBuilder::Channel(eastl::string_view name, uint32_t index, Format format)
    {
        return Channel(ShaderSemantic(name, index), format);
    }

    InputStreamLayoutBuilder::BufferDescriptorBuilder* InputStreamLayoutBuilder::BufferDescriptorBuilder::Padding(uint32_t byteCount)
    {
        m_byteOffset += byteCount;
        return this;
    }

    InputStreamLayoutBuilder::BufferDescriptorBuilder* InputStreamLayoutBuilder::AddBuffer(StreamStepFunction stepFunction, uint32_t stepRate)
    {
        if (m_bufferDescriptorBuilders.size() == m_bufferDescriptorBuilders.capacity())
        {
            LOG_ERROR("[InputStreamLayoutBuilder] No space to add stream buffer.");

            // Return a dummy so that subsequent calls to register channels won't crash and don't require null check clutter.
            return &m_dummyBufferDescriptorBuilder;
        }

        BufferDescriptorBuilder* builder = &m_bufferDescriptorBuilders.emplace_back();
        builder->m_bufferIndex = static_cast<uint32_t>(m_bufferDescriptorBuilders.size() - 1);
        builder->m_bufferDescriptor.m_stepFunction = stepFunction;
        builder->m_bufferDescriptor.m_stepRate = stepRate;

        return builder;
    }
}