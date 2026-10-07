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

    /// The one vertex format a mesh has once compiled, whatever its source carried:
    /// POSITION float3, NORMAL float3, TANGENT float4 (w is the handedness), TEXCOORD0
    /// float2, interleaved in that order. The render layer relies on it: every mesh is
    /// drawn through the same input layout, and meshes share one vertex binding.
    namespace StandardVertex
    {
        inline constexpr uint32_t PositionOffset = 0;
        inline constexpr uint32_t NormalOffset   = 12;
        inline constexpr uint32_t TangentOffset  = 24;
        inline constexpr uint32_t TexCoordOffset = 40;
        inline constexpr uint32_t Stride         = 48;
    }

    inline bool IsStandardVertexLayout(const VertexLayout& layout)
    {
        auto has = [&](const char* semantic, RHI::Format format, uint32_t byteOffset)
        {
            const VertexAttribute* attribute = layout.FindAttribute(semantic);
            return attribute != nullptr && attribute->format == format && attribute->byteOffset == byteOffset;
        };

        return layout.stride == StandardVertex::Stride
            && layout.attributes.size() == 4
            && has(VertexSemantic::Position, RHI::Format::R32G32B32_FLOAT,    StandardVertex::PositionOffset)
            && has(VertexSemantic::Normal,   RHI::Format::R32G32B32_FLOAT,    StandardVertex::NormalOffset)
            && has(VertexSemantic::Tangent,  RHI::Format::R32G32B32A32_FLOAT, StandardVertex::TangentOffset)
            && has(VertexSemantic::TexCoord, RHI::Format::R32G32_FLOAT,       StandardVertex::TexCoordOffset);
    }
}
