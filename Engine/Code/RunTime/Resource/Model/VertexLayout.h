#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <RHI/Format.h>

namespace Spark::Resource
{
    /// Describes one attribute field within an interleaved vertex buffer.
    /// Maps to RHI::StreamChannelDescriptor at the render layer.
    struct VertexAttribute
    {
        eastl::string   semantic;      // "POSITION", "NORMAL", "TANGENT", "TEXCOORD0", ...
        RHI::Format     format;        // element format, e.g. R32G32B32_FLOAT
        uint32_t        byteOffset;    // offset from start of vertex in bytes
    };

    /// Describes the memory layout of the vertex buffer produced by the
    /// ModelAssetCompiler. The render layer converts this to RHI::InputStreamLayout
    /// when creating pipeline state.
    struct VertexLayout
    {
        uint32_t                         stride = 0;
        eastl::vector<VertexAttribute>   attributes;
    };
}
