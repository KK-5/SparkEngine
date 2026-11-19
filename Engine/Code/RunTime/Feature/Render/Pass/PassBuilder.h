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
    //! Build the RHI layout and record which spaces the pass takes from shared bindings.
    //! The RHI layout contains the full input declarations with the pass's stage visibility.
    //! The component's layout is null when no shader is present.
    PassPipelineLayout BuildPassPipelineLayout(
        RHI::Factory& factory, const PassShaders& shaders, const char* passName);

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
    



    // The builders live in <Pass/RenderPass.h> (RenderPassBuilder, SPARK_RENDER_PASS),
    // <Pass/ComputePass.h> (ComputePassBuilder, SPARK_COMPUTE_PASS), <Pass/CopyPass.h>
    // (CopyPassBuilder, SPARK_COPY_PASS) and <Pass/AccelerationStructurePass.h>
    // (AccelerationStructurePassBuilder, SPARK_ACCELERATION_STRUCTURE_PASS). RenderPass.h pulls
    // PassCapabilities.h, kept out of this common header so only files that write render
    // passes pay for it.
} // namespace Spark::Render
