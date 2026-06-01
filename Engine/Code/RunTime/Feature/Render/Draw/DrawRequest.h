#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>

#include <RHI/Command/DrawArguments.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::Render
{
    struct VertexBufferInfo
    {
        uint32_t m_byteOffset = 0;
        uint32_t m_byteCount = 0;
        uint32_t m_byteStride = 0;
    };

    struct IndexBufferInfo
    {
        uint32_t m_byteOffset = 0;
        uint32_t m_byteCount = 0;
        RHI::IndexFormat m_format = RHI::IndexFormat::UINT32;
    };

    //! Declarative draw "recipe" filled by user code during the Build phase.
    //! RenderGraphCompiler::CompileDrawRequests translates each DrawRequest
    //! into a compiled RHI::DrawItem with resolved PSO and ShaderBindings pointers.
    struct DrawRequest
    {
        DrawRequest() = default;

        // Draw call parameters (mirrored directly to DrawItem).
        RHI::DrawArguments         m_drawArguments;
        RHI::DrawInstanceArguments m_drawInstanceArgs;
        uint8_t                    m_stencilRef = 0;

        // Geometry: RHIHandle entities carrying VertexBufferView / IndexBufferView
        // in RHIContext. Compiler resolves these to the view structs for DrawItem.
        VertexBufferInfo m_vertexBufferInfo;
        IndexBufferInfo  m_indexBufferInfo;
        RHI::RHIHandle m_vertexBufferView = RHI::NullHandle;
        RHI::RHIHandle m_indexBufferView  = RHI::NullHandle;

        // Per-draw ShaderBindings entities. User creates them via
        // CreateShaderBindings(), populates shader inputs, and marks dirty.
        // Compiler resolves each entity's compiled ShaderBindings pointer.
        uint8_t m_shaderBindingsCount = 0;
        eastl::fixed_vector<RHI::RHIHandle, RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_shaderBindingEntities;

        // Optional per-draw PSO variant: override the pass-level vertex/fragment
        // shader and/or fixed-function render states. When both are default
        // (nullptr / nullopt), the draw inherits the pass PSO.
        Ptr<Resource::ShaderAsset>       m_vertexShaderOverride;
        Ptr<Resource::ShaderAsset>       m_fragmentShaderOverride;
        eastl::optional<RHI::RenderStates> m_renderStatesOverride;
    };
}
