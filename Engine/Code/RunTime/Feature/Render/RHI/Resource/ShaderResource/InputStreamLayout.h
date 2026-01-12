#pragma once

#include <EASTL/span.h>
#include <EASTL/fixed_vector.h>

#include <RHI/Format.h>
#include <RHI/RHILimits.h>

#include "ShaderSemantic.h"

namespace Spark::RHI
{
    enum class PrimitiveTopology : uint32_t
    {
        Undefined = 0,
        PointList,
        LineList,
        LineListAdj,
        LineStrip,
        LineStripAdj,
        TriangleList,
        TriangleListAdj,
        TriangleStrip,
        TriangleStripAdj,
        PatchList
    };

    enum class StreamStepFunction : uint32_t
    {
        Constant = 0,
        PerVertex,
        PerInstance,
        PerPatch,
        PerPatchControlPoint
    };

    class StreamChannelDescriptor
    {
    public:
        StreamChannelDescriptor() = default;

        StreamChannelDescriptor(
            ShaderSemantic semantic,
            Format format,
            uint32_t byteOffset,
            uint32_t bufferIndex);

        size_t GetHash() const;
        
        ShaderSemantic m_semantic;

        Format m_format = Format::Unknown;

        uint32_t m_bufferIndex = 0;

        uint32_t m_byteOffset = 0;
    };

    class StreamBufferDescriptor
    {
    public:
        StreamBufferDescriptor() = default;

        StreamBufferDescriptor(
            StreamStepFunction stepFunction,
            uint32_t stepRate,
            uint32_t byteStride);
        
        size_t GetHash() const;

        StreamStepFunction m_stepFunction = StreamStepFunction::PerVertex;

        uint32_t m_stepRate = 1;

        uint32_t m_byteStride = 0;
    };

    class InputStreamLayout
    {
    public:
        InputStreamLayout() = default;

        void Clear();

        bool Finalize();

        bool IsFinalized() const;

        void SetTopology(PrimitiveTopology topology);

        void AddStreamChannel(const StreamChannelDescriptor& descriptor);

        void AddStreamBuffer(const StreamBufferDescriptor& descriptor);

        const PrimitiveTopology GetTopology() const;

        eastl::span<const StreamChannelDescriptor> GetStreamChannels() const;

        eastl::span<const StreamBufferDescriptor> GetStreamBuffers() const;

        size_t GetHash() const;

        bool operator == (const InputStreamLayout& rhs) const;

    private:
        PrimitiveTopology m_topology = PrimitiveTopology::Undefined;

        eastl::fixed_vector<StreamChannelDescriptor, Limits::Pipeline::StreamChannelCountMax> m_streamChannels;

        eastl::fixed_vector<StreamBufferDescriptor, Limits::Pipeline::StreamCountMax> m_streamBuffers;

        size_t m_hash = 0 ;
    };
}