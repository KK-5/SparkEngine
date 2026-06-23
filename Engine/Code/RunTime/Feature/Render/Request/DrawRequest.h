#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/optional.h>

#include <RHI/Command/DrawArguments.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>

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

    //! Binds a pass attachment's view into one of the draw's ShaderBindings at
    //! compile time. The attachment (e.g. a transient SceneColor) does not exist
    //! when the DrawRequest is built, so it is referenced by slot name; the view
    //! is resolved per-pass during Compile (CompilePassDrawBindings) and written
    //! into the target ShaderBindings' image input via SetView.
    struct AttachmentBinding
    {
        RHI::InputName m_slot;           //!< Pass attachment slot, e.g. "SceneColor".
        RHI::InputName m_shaderInput;    //!< Shader image input name, e.g. "g_SceneColor".
        uint8_t        m_groupIndex = 0; //!< Which m_shaderBindingEntities entry receives the view.
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

        // Geometry: RHIHandle entities carrying a Components::Buffer in RHIContext.
        // Compiler reads each buffer and builds the VertexInputView / IndexBufferView
        // for the DrawItem from the m_*BufferInfo offsets/stride.
        VertexBufferInfo m_vertexBufferInfo;
        IndexBufferInfo  m_indexBufferInfo;
        RHI::RHIHandle   m_vertexBuffer = RHI::NullHandle;
        RHI::RHIHandle   m_indexBuffer  = RHI::NullHandle;

        // Optional per-instance vertex stream, bound at the slot AFTER the mesh VB
        // (slot 1): the shared instance-ID buffer feeding the INSTANCE_INDEX semantic.
        // NullHandle = single-stream draw. See InstanceBindingSystem / plan §2.5.
        // NOTE: minimal v1 carrier — the general multi-stream DrawRequest shape is a
        // separate design cycle (plan §2.5 / §6), so this stays one named stream.
        RHI::RHIHandle   m_instanceIdBuffer = RHI::NullHandle;
        VertexBufferInfo m_instanceIdBufferInfo;


        uint8_t m_scissorsCount  = 0;
        uint8_t m_viewportsCount = 0;
        eastl::fixed_vector<RHI::Viewport, RHI::Limits::Pipeline::AttachmentColorCountMax> m_viewports;
        eastl::fixed_vector<RHI::Scissor,  RHI::Limits::Pipeline::AttachmentColorCountMax> m_scissors;

        // ShaderBindings entities this draw binds, in space order. These are
        // REFERENCES, not owned — the entity's lifetime belongs to whoever produced
        // it (today the Processor; later a View / mesh / Material owning a shared,
        // reusable binding). Compiler resolves each entity's ShaderBindings pointer
        // into the DrawItem.
        eastl::fixed_vector<RHI::RHIHandle, RHI::Limits::Pipeline::ShaderInputGroupCountMax> m_shaderBindingEntities;

        // Pass attachments this draw samples, injected into m_shaderBindingEntities
        // at compile time (CompilePassDrawBindings). Empty for draws that sample no
        // pass-produced resource (e.g. a depth prepass that only writes). N is a
        // soft cap (fixed_vector spills to heap); revisit once real sampling passes land.
        eastl::fixed_vector<AttachmentBinding, 8> m_attachmentBindings;

        // Optional per-draw PSO variant: override the pass-level vertex/fragment
        // shader and/or fixed-function render states. When both are default
        // (nullptr / nullopt), the draw inherits the pass PSO.
        Ptr<Resource::ShaderAsset>         m_vertexShaderOverride;
        Ptr<Resource::ShaderAsset>         m_fragmentShaderOverride;
        eastl::optional<RHI::RenderStates> m_renderStatesOverride;
    };
}
