#include "PipelineVariantInterpret.h"

namespace Spark::Render
{
    namespace
    {
        //! The macros of the features a key carries, whether or not a template implements them.
        FeatureMacro FeatureMacrosOf(PipelineKey key)
        {
            FeatureMacro macros = FeatureMacro::None;
            if (GetAlphaMode(key) == Resource::AlphaMode::Mask)
            {
                macros |= FeatureMacro::MaterialBlendingMasked;
            }
            return macros;
        }

        //! A pixel shader that may discard keeps depth from being written ahead of it, so the
        //! groups using one go after those that fill depth with early-Z.
        constexpr FeatureMacro EarlyDepthBreakingMacros = FeatureMacro::MaterialBlendingMasked;

        constexpr uint32_t AfterEarlyDepthPriority = 1;
    }

    PipelineVariant InterpretPipelineVariant(
        PipelineKey key, const RHI::RenderStates& passStates, const ShaderTemplateFeatures& shaderTemplate)
    {
        PipelineVariant variant;

        // Rules every pass shares, in this order: each may build on the ones before it.
        if (IsDoubleSided(key))
        {
            variant.m_cullMode = RHI::CullMode::None;
        }

        // The features the template implements; each of them so far reads the material's code.
        variant.m_featureMacros = FilterBits(FeatureMacrosOf(key), shaderTemplate.m_featureMacros);
        if (!shaderTemplate.m_ignoresMaterialShader || variant.m_featureMacros != FeatureMacro::None)
        {
            variant.m_materialShader = GetShaderIndex(key);
        }

        if (CheckBitsAny(variant.m_featureMacros, EarlyDepthBreakingMacros))
        {
            variant.m_priority = AfterEarlyDepthPriority;
        }

        // What the pass does anyway is no change: variants that draw alike compare equal.
        if (variant.m_cullMode == passStates.m_rasterState.m_cullMode)
        {
            variant.m_cullMode = RHI::CullMode::Invalid;
        }

        return variant;
    }
}
