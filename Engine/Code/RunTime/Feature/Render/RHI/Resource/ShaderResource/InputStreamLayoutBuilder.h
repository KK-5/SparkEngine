#pragma once

#include "InputStreamLayout.h"

namespace Spark::Render::RHI
{
    class InputStreamLayoutBuilder
    {
    public:
        class BufferDescriptorBuilder
        {
            friend class InputStreamLayoutBuilder;

        public:
            BufferDescriptorBuilder* Channel(eastl::string_view name, uint32_t index, Format format);

            BufferDescriptorBuilder* Channel(const ShaderSemantic& semantic, Format format);

            BufferDescriptorBuilder* Padding(uint32_t byteCount);

        private:
            uint32_t m_bufferIndex = 0;
            uint32_t m_byteOffset = 0;
            eastl::fixed_vector<StreamChannelDescriptor, Limits::Pipeline::StreamChannelCountMax> m_channelDescriptors;
            StreamBufferDescriptor m_bufferDescriptor;
        };

        InputStreamLayoutBuilder();

        void Begin();

        void SetTopology(PrimitiveTopology topology);

        BufferDescriptorBuilder* AddBuffer(StreamStepFunction stepFunction = StreamStepFunction::PerVertex, uint32_t stepRate = 1);

        InputStreamLayout End();

    private:
        PrimitiveTopology m_topology = PrimitiveTopology::TriangleList;

        eastl::fixed_vector<BufferDescriptorBuilder, Limits::Pipeline::StreamCountMax> m_bufferDescriptorBuilders;
        BufferDescriptorBuilder m_dummyBufferDescriptorBuilder;
    };
}