#include "PipelineStateBuild.h"

#include <Log/ILogSystem.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>

#include "PipelineKey.h"

namespace Spark::Render
{
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
        const PassStageFunctions& stageFunctions,
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
        descriptor.m_vertexFunction   = stageFunctions.m_vertex;
        descriptor.m_fragmentFunction = stageFunctions.m_fragment;
        descriptor.m_geometryFunction = stageFunctions.m_geometry;

        descriptor.m_inputStreamLayout        = pipelineState.m_inputStreamLayout;
        descriptor.m_renderTargetLayout       = pipelineState.m_renderTargetLayout;
        descriptor.m_renderStates             = ApplyPipelineVariant(pipelineState.m_renderStates, variant);
        descriptor.m_pipelineLayoutDescriptor = layout.m_layout;
        return descriptor;
    }
}
