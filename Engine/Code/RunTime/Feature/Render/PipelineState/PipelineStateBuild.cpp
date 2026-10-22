#include "PipelineStateBuild.h"

#include <Log/ILogSystem.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>

#include "PipelineKey.h"

namespace Spark::Render
{
    namespace
    {
        //! The colour over what is behind it by the mode; SceneColor's alpha is kept, as the
        //! lighting passes keep it. The pass's write mask stays.
        void ApplyBlendMode(BlendMode mode, RHI::TargetBlendState& target)
        {
            target.m_enable           = 1;
            target.m_blendOp          = RHI::BlendOp::Add;
            target.m_blendAlphaSource = RHI::BlendFactor::Zero;
            target.m_blendAlphaDest   = RHI::BlendFactor::One;
            target.m_blendAlphaOp     = RHI::BlendOp::Add;

            switch (mode)
            {
            case BlendMode::Translucent:
                target.m_blendSource = RHI::BlendFactor::AlphaSource;
                target.m_blendDest   = RHI::BlendFactor::AlphaSourceInverse;
                break;
            case BlendMode::Additive:
                target.m_blendSource = RHI::BlendFactor::AlphaSource;
                target.m_blendDest   = RHI::BlendFactor::One;
                break;
            case BlendMode::AlphaComposite:
                target.m_blendSource = RHI::BlendFactor::One;
                target.m_blendDest   = RHI::BlendFactor::AlphaSourceInverse;
                break;
            case BlendMode::Invalid:
                ASSERT(false, "A blend mode to apply must be valid.");
                break;
            }
        }
    }

    RHI::RenderStates ApplyPipelineVariant(const RHI::RenderStates& passStates, const PipelineVariant& variant)
    {
        RHI::RenderStates states = passStates;
        if (variant.m_cullMode != RHI::CullMode::Invalid)
        {
            states.m_rasterState.m_cullMode = variant.m_cullMode;
        }
        if (variant.m_blendMode != BlendMode::Invalid)
        {
            // Without independent blending every target follows the first, on DX12 by reading
            // only it and on Vulkan by requiring them all alike.
            ASSERT(!states.m_blendState.m_independentBlendEnable,
                "A blend mode is for a pass whose targets blend alike.");
            ApplyBlendMode(variant.m_blendMode, states.m_blendState.m_targets[0]);
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
