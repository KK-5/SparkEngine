#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <RHI/Format.h>

namespace Spark::Resource
{
    /// Canonical semantic names used throughout the asset pipeline. Any code
    /// that writes `VertexAttribute::semantic` or compares against it must
    /// reference these constants instead of inlining string literals, so a
    /// rename only needs to touch this header.
    namespace VertexSemantic
    {
        inline constexpr const char* Position = "POSITION";
        inline constexpr const char* Normal   = "NORMAL";
        inline constexpr const char* Tangent  = "TANGENT";
        inline constexpr const char* TexCoord = "TEXCOORD";
        inline constexpr const char* Color    = "COLOR";
        inline constexpr const char* Joints   = "JOINTS";
        inline constexpr const char* Weights  = "WEIGHTS";
    }

    /// Describes one attribute field within an interleaved vertex buffer.
    /// Maps to RHI::StreamChannelDescriptor at the render layer.
    struct VertexAttribute
    {
        eastl::string   semantic;          // see VertexSemantic
        uint32_t        semanticIndex = 0; // 0 for most; >0 for TEXCOORD_1, JOINTS_1 etc.
        RHI::Format     format;            // element format, e.g. R32G32B32_FLOAT
        uint32_t        byteOffset;        // offset from start of vertex in bytes
    };

    /// Describes the memory layout of the vertex buffer produced by the
    /// ModelAssetCompiler. The render layer converts this to RHI::InputStreamLayout
    /// when creating pipeline state.
    struct VertexLayout
    {
        uint32_t                         stride = 0;
        eastl::vector<VertexAttribute>   attributes;

        /// Returns the first attribute whose semantic + index match, or nullptr.
        const VertexAttribute* FindAttribute(const char* semantic, uint32_t index = 0) const
        {
            for (const VertexAttribute& a : attributes)
            {
                if (a.semantic == semantic && a.semanticIndex == index)
                {
                    return &a;
                }
            }
            return nullptr;
        }
    };
}
