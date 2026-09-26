#pragma once

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
#include <Resource/Shader/ShaderBuilder.h>

namespace Spark::Render
{
    //! Build a PipelineLayoutDescriptor from a PassShaders set via shader
    //! reflection. Returns nullptr if no shader is present (a pass without a
    //! pipeline). The result is owned by the caller; PassBuilder stores it on
    //! the pass as PassPipelineLayout so user Build callbacks can grab it
    //! before the render-graph Compile phase runs.
    inline Ptr<RHI::PipelineLayoutDescriptor> BuildPipelineLayoutFromShaders(
        RHI::Factory& factory, const PassShaders& shaders)
    {
        const Resource::ShaderAsset* assets[] = {
            shaders.m_vertexShader.get(),
            shaders.m_fragmentShader.get(),
            shaders.m_geometryShader.get(),
            shaders.m_computeShader.get(),
        };
        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(
            eastl::span<const Resource::ShaderAsset* const>(assets, 4));

        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            return nullptr;
        }

        auto layout = factory.CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list);
        layout->Finalize();
        return layout;
    }

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

    // The builders live in <Pass/RenderPass.h> (RenderPassBuilder, SPARK_RENDER_PASS) and
    // <Pass/ComputePass.h> (ComputePassBuilder, SPARK_COMPUTE_PASS). RenderPass.h pulls
    // PassCapabilities.h, kept out of this common header so only files that write render
    // passes pay for it.
} // namespace Spark::Render
