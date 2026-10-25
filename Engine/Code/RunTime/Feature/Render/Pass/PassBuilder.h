#pragma once

#include <EASTL/span.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <Base.h>

#include <RHI/Factory.h>
#include <RHI/Format.h>
#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/RenderStates.h>

#include <Pass/Pass.h>
#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/Component/PassComponents.h>
#include <RenderGraph/PassScopes.h>

namespace Spark::Render
{
    //! The pass's PipelineLayoutDescriptor. Each shared group it binds (`groups`, from
    //! .Binds) gives its whole space, copied from the group's own layout so the pass's
    //! descriptor tables are the ones the group writes, however little of it the shaders
    //! read; the space is made visible to every stage the pass has. The pass's own space
    //! (kPerPassSpaceId) and its root constants come from its shaders' reflection, which
    //! is also checked against the groups. Null if no shader is present (a pass without a
    //! pipeline). PassBuilder stores it on the pass as PassPipelineLayout so user Build
    //! callbacks can grab it before the render-graph Compile phase runs. Defined in
    //! PassBuilder.cpp.
    Ptr<RHI::PipelineLayoutDescriptor> BuildPassPipelineLayout(
        RHI::Factory& factory, const PassShaders& shaders,
        eastl::span<const RHI::ShaderBindings* const> groups, const char* passName);

    //! A stage function for each shader the pass sets. The pass builders' Finalize stores it on
    //! the pass. Defined in PassBuilder.cpp.
    PassStageFunctions CreatePassStageFunctions(
        RHI::Factory& factory, const PassShaders& shaders, const char* passName);

    struct RenderPassConfig
    {
        Ptr<Resource::ShaderAsset> m_vertexShader = nullptr;
        Ptr<Resource::ShaderAsset> m_fragmentShader = nullptr;
        Ptr<Resource::ShaderAsset> m_geometryShader = nullptr;
        RHI::InputStreamLayout     m_inputLayout {};
        RHI::RenderStates          m_renderStates {};
        RHI::RenderTargetLayout    m_renderTargetLayout {};
        RHI::MultisampleState      m_multisampleState {};
    };
    



    //! Allocate the pass's own per-pass (space2) ShaderBindings against its reflected layout
    //! and record it on the pass as PassBindings. The pass builders' Finalize calls this when
    //! the layout declares the space. The entity is created with ShaderBindingsUpdateTag so
    //! the first compile sweep produces valid GPU bindings before the first execute, and with
    //! PassShaderBindingsTag so teardown can reap it. It owns the binding's lifetime
    //! (Components::ShaderBindings holds the Ptr).
    //!
    //! Returns NullHandle, leaving the pass without PassBindings, if the pass has no
    //! PassPipelineLayout or if RHI services / Init fail. Defined in PassAccess.cpp.
    RHIHandle CreatePassBindings(PassContext& passCtx, RHIContext& rhiCtx, Pass pass);

    // The builders live in <Pass/RenderPass.h> (RenderPassBuilder, SPARK_RENDER_PASS),
    // <Pass/ComputePass.h> (ComputePassBuilder, SPARK_COMPUTE_PASS) and <Pass/CopyPass.h>
    // (CopyPassBuilder, SPARK_COPY_PASS). RenderPass.h pulls
    // PassCapabilities.h, kept out of this common header so only files that write render
    // passes pay for it.
} // namespace Spark::Render
