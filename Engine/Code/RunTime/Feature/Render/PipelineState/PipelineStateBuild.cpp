#include "PipelineStateBuild.h"

#include <Log/ILogSystem.h>

#include <RHI/Factory.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/ShaderStages.h>

#include "PipelineKey.h"

namespace Spark::Render
{
    namespace
    {
        //! `stage` of `shader` as a stage function, or null when the pass sets no such shader.
        Ptr<RHI::ShaderStageFunction> CreateStageFunction(
            RHI::Factory& factory, const Ptr<Resource::ShaderAsset>& shader, RHI::ShaderStage stage,
            const char* passName, const char* stageName)
        {
            if (!shader)
            {
                return nullptr;
            }

            const auto* bytecode = shader->GetShaderData()->GetStageBytecode(stage);
            ASSERT(bytecode, "Pass '{}': its {} has no bytecode for that stage.", passName, stageName);
            auto function = factory.CreateShaderStageFunction(stage);
            function->SetByteCode(bytecode->bytecode);
            function->Finalize();
            return function;
        }
    }

    RHI::RenderStates ApplyPipelineVariant(const RHI::RenderStates& passStates, const PipelineVariant& variant)
    {
        RHI::RenderStates states = passStates;
        if (variant.m_cullMode != RHI::CullMode::Invalid)
        {
            states.m_rasterState.m_cullMode = variant.m_cullMode;
        }
        return states;
    }

    RHI::PipelineStateDescriptorForDraw BuildDrawPipelineDescriptor(
        RHI::Factory&             factory,
        const PassShaders&        shaders,
        const PassPipelineState&  pipelineState,
        const PassPipelineLayout& layout,
        const PipelineVariant&    variant,
        const char*               passName)
    {
        // The one material shader there is, StandardPBR, is written into every template.
        ASSERT(variant.m_featureMacros == FeatureMacro::None
                && (variant.m_materialShader == PipelineVariant::NoMaterialShader || variant.m_materialShader < PipelineShaderCount),
            "Pass '{}': a variant that picks shaders needs shader permutations, which do not exist yet.", passName);

        RHI::PipelineStateDescriptorForDraw descriptor;
        descriptor.m_vertexFunction =
            CreateStageFunction(factory, shaders.m_vertexShader, RHI::ShaderStage::Vertex, passName, "VertexShader");
        descriptor.m_fragmentFunction =
            CreateStageFunction(factory, shaders.m_fragmentShader, RHI::ShaderStage::Fragment, passName, "FragmentShader");
        descriptor.m_geometryFunction =
            CreateStageFunction(factory, shaders.m_geometryShader, RHI::ShaderStage::Geometry, passName, "GeometryShader");

        descriptor.m_inputStreamLayout        = pipelineState.m_inputStreamLayout;
        descriptor.m_renderTargetLayout       = pipelineState.m_renderTargetLayout;
        descriptor.m_renderStates             = ApplyPipelineVariant(pipelineState.m_renderStates, variant);
        descriptor.m_pipelineLayoutDescriptor = layout.m_layout;
        return descriptor;
    }
}
