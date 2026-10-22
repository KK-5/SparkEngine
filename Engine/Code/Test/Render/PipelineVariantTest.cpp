#include <gtest/gtest.h>

#include <PipelineState/PipelineKey.h>
#include <PipelineState/PipelineStateBuild.h>
#include <PipelineState/PipelineVariantInterpret.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    Resource::MaterialState State(Resource::AlphaMode alphaMode, bool doubleSided)
    {
        Resource::MaterialState state;
        state.m_alphaMode   = alphaMode;
        state.m_doubleSided = doubleSided;
        return state;
    }

    //! A pass that culls back faces, as the scene passes do.
    RHI::RenderStates BackCulling()
    {
        RHI::RenderStates states;
        states.m_rasterState.m_cullMode = RHI::CullMode::Back;
        return states;
    }

    //! A template whose own code stands in for the material's, as a depth-only one does.
    ShaderTemplateFeatures DepthTemplate()
    {
        ShaderTemplateFeatures shaderTemplate;
        shaderTemplate.m_ignoresMaterialShader = true;
        return shaderTemplate;
    }

    constexpr Resource::AlphaMode AlphaModes[] = {
        Resource::AlphaMode::Opaque, Resource::AlphaMode::Mask, Resource::AlphaMode::Blend,
        Resource::AlphaMode::Additive, Resource::AlphaMode::AlphaComposite };
}

TEST(PipelineKeyTest, AKeyDecodesToWhatItWasMadeOf)
{
    for (uint32_t shaderIndex = 0; shaderIndex < PipelineShaderCount; ++shaderIndex)
    {
        for (Resource::AlphaMode alphaMode : AlphaModes)
        {
            for (bool doubleSided : { false, true })
            {
                const PipelineKey key = MakePipelineKey(shaderIndex, State(alphaMode, doubleSided));
                EXPECT_LT(key.m_value, PipelineKeyCount);
                EXPECT_EQ(GetShaderIndex(key), shaderIndex);
                EXPECT_EQ(GetAlphaMode(key), alphaMode);
                EXPECT_EQ(IsDoubleSided(key), doubleSided);
            }
        }
    }
}

TEST(PipelineKeyTest, DifferentFeaturesMakeDifferentKeys)
{
    EXPECT_NE(MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false)),
              MakePipelineKey(0, State(Resource::AlphaMode::Opaque, true)));
    EXPECT_NE(MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false)),
              MakePipelineKey(0, State(Resource::AlphaMode::Mask, false)));
    // The default material state is key 0, which a reset g_Instances record also reads as.
    EXPECT_EQ(MakePipelineKey(0, Resource::MaterialState{}).m_value, 0u);
}

TEST(PipelineVariantTest, ASingleSidedOpaqueKeyChangesNothing)
{
    const PipelineVariant variant = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false)), BackCulling(), DepthTemplate());
    EXPECT_EQ(variant, PipelineVariant{});
}

TEST(PipelineVariantTest, ADoubleSidedKeyCullsNothing)
{
    const PipelineVariant variant = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Opaque, true)), BackCulling(), DepthTemplate());
    EXPECT_EQ(variant.m_cullMode, RHI::CullMode::None);
    EXPECT_EQ(variant.m_priority, 0u);
    EXPECT_EQ(variant.m_materialShader, PipelineVariant::NoMaterialShader);
}

//! In a pass that culls nothing already, being double-sided is no change.
TEST(PipelineVariantTest, ACullModeThePassAlreadyHasIsNoChange)
{
    RHI::RenderStates noCulling;
    noCulling.m_rasterState.m_cullMode = RHI::CullMode::None;

    const PipelineVariant variant = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Opaque, true)), noCulling, DepthTemplate());
    EXPECT_EQ(variant, PipelineVariant{});
}

//! A template that does not handle masking draws a masked key as it draws an opaque one.
TEST(PipelineVariantTest, AFeatureTheTemplateIgnoresIsNoChange)
{
    const PipelineVariant opaque = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false)), BackCulling(), DepthTemplate());
    const PipelineVariant masked = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Mask, false)), BackCulling(), DepthTemplate());
    EXPECT_EQ(opaque, masked);
    EXPECT_EQ(eastl::hash<PipelineVariant>{}(opaque), eastl::hash<PipelineVariant>{}(masked));
}

TEST(PipelineVariantTest, AMaskingTemplateClipsWithTheMaterialAndDrawsLater)
{
    ShaderTemplateFeatures masking = DepthTemplate();
    masking.m_featureMacros = FeatureMacro::MaterialBlendingMasked;

    const PipelineVariant masked = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Mask, false)), BackCulling(), masking);
    EXPECT_EQ(masked.m_featureMacros, FeatureMacro::MaterialBlendingMasked);
    EXPECT_EQ(masked.m_materialShader, 0u);
    EXPECT_GT(masked.m_priority, 0u);

    // An opaque key in the same pass needs neither.
    const PipelineVariant opaque = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false)), BackCulling(), masking);
    EXPECT_EQ(opaque, PipelineVariant{});
}

TEST(PipelineVariantTest, ATemplateTakesTheMaterialShaderUnlessItIgnoresIt)
{
    const PipelineKey key = MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false));

    const PipelineVariant surface = InterpretPipelineVariant(key, BackCulling(), ShaderTemplateFeatures{});
    EXPECT_EQ(surface.m_materialShader, 0u);
    EXPECT_EQ(surface.m_featureMacros, FeatureMacro::None);
    EXPECT_EQ(surface.m_priority, 0u);

    const PipelineVariant depth = InterpretPipelineVariant(key, BackCulling(), DepthTemplate());
    EXPECT_EQ(depth.m_materialShader, PipelineVariant::NoMaterialShader);
}

TEST(PipelineVariantTest, ApplyingAVariantChangesOnlyWhatItOverrides)
{
    RHI::RenderStates passStates = BackCulling();
    passStates.m_rasterState.m_depthBias               = 4;
    passStates.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;

    EXPECT_EQ(ApplyPipelineVariant(passStates, PipelineVariant{}), passStates);

    PipelineVariant doubleSided;
    doubleSided.m_cullMode = RHI::CullMode::None;
    const RHI::RenderStates applied = ApplyPipelineVariant(passStates, doubleSided);
    EXPECT_EQ(applied.m_rasterState.m_cullMode, RHI::CullMode::None);
    EXPECT_EQ(applied.m_rasterState.m_depthBias, 4);
    EXPECT_EQ(applied.m_depthStencilState, passStates.m_depthStencilState);
    EXPECT_EQ(applied.m_blendState, passStates.m_blendState);
}

namespace
{
    //! A pass blending as TranslucencyPass does by default.
    RHI::RenderStates TranslucentBlending()
    {
        RHI::RenderStates states = BackCulling();
        auto& blend = states.m_blendState.m_targets[0];
        blend.m_enable           = 1;
        blend.m_blendSource      = RHI::BlendFactor::AlphaSource;
        blend.m_blendDest        = RHI::BlendFactor::AlphaSourceInverse;
        blend.m_blendAlphaSource = RHI::BlendFactor::Zero;
        blend.m_blendAlphaDest   = RHI::BlendFactor::One;
        return states;
    }

    BlendMode BlendModeIn(Resource::AlphaMode alphaMode, const RHI::RenderStates& passStates)
    {
        return InterpretPipelineVariant(MakePipelineKey(0, State(alphaMode, false)), passStates,
                                        ShaderTemplateFeatures{}).m_blendMode;
    }
}

TEST(PipelineVariantTest, ATranslucentKeyBlendsByItsAlphaMode)
{
    EXPECT_EQ(BlendModeIn(Resource::AlphaMode::Blend, BackCulling()), BlendMode::Translucent);
    EXPECT_EQ(BlendModeIn(Resource::AlphaMode::Additive, BackCulling()), BlendMode::Additive);
    EXPECT_EQ(BlendModeIn(Resource::AlphaMode::AlphaComposite, BackCulling()), BlendMode::AlphaComposite);
}

TEST(PipelineVariantTest, AKeyThatDoesNotBlendKeepsThePassBlending)
{
    for (const RHI::RenderStates& passStates : { BackCulling(), TranslucentBlending() })
    {
        EXPECT_EQ(BlendModeIn(Resource::AlphaMode::Opaque, passStates), BlendMode::Invalid);
        EXPECT_EQ(BlendModeIn(Resource::AlphaMode::Mask, passStates), BlendMode::Invalid);
    }
}

//! So blended surfaces in the translucent pass draw with its own PSO, in one group.
TEST(PipelineVariantTest, ABlendModeThePassAlreadyHasIsNoChange)
{
    const PipelineVariant blend = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Blend, false)), TranslucentBlending(), ShaderTemplateFeatures{});
    const PipelineVariant opaque = InterpretPipelineVariant(
        MakePipelineKey(0, State(Resource::AlphaMode::Opaque, false)), TranslucentBlending(), ShaderTemplateFeatures{});
    EXPECT_EQ(blend, opaque);

    EXPECT_EQ(BlendModeIn(Resource::AlphaMode::Additive, TranslucentBlending()), BlendMode::Additive);
    EXPECT_EQ(BlendModeIn(Resource::AlphaMode::AlphaComposite, TranslucentBlending()), BlendMode::AlphaComposite);
}

TEST(PipelineVariantTest, ABlendModeSetsTheColourFactorsAndKeepsTheRest)
{
    RHI::RenderStates passStates = TranslucentBlending();
    passStates.m_blendState.m_targets[0].m_writeMask    = 0x7;
    passStates.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::Zero;

    PipelineVariant additive;
    additive.m_blendMode = BlendMode::Additive;
    const RHI::RenderStates added = ApplyPipelineVariant(passStates, additive);
    const RHI::TargetBlendState& addedTarget = added.m_blendState.m_targets[0];
    EXPECT_EQ(addedTarget.m_blendSource, RHI::BlendFactor::AlphaSource);
    EXPECT_EQ(addedTarget.m_blendDest, RHI::BlendFactor::One);

    PipelineVariant composite;
    composite.m_blendMode = BlendMode::AlphaComposite;
    const RHI::RenderStates composited = ApplyPipelineVariant(passStates, composite);
    const RHI::TargetBlendState& compositedTarget = composited.m_blendState.m_targets[0];
    EXPECT_EQ(compositedTarget.m_blendSource, RHI::BlendFactor::One);
    EXPECT_EQ(compositedTarget.m_blendDest, RHI::BlendFactor::AlphaSourceInverse);

    for (const RHI::RenderStates* applied : { &added, &composited })
    {
        const RHI::TargetBlendState& target = applied->m_blendState.m_targets[0];
        EXPECT_EQ(target.m_enable, 1u);
        EXPECT_EQ(target.m_blendOp, RHI::BlendOp::Add);
        EXPECT_EQ(target.m_blendAlphaSource, RHI::BlendFactor::Zero);
        EXPECT_EQ(target.m_blendAlphaDest, RHI::BlendFactor::One);
        EXPECT_EQ(target.m_writeMask, 0x7u);
        EXPECT_EQ(applied->m_rasterState, passStates.m_rasterState);
        EXPECT_EQ(applied->m_depthStencilState, passStates.m_depthStencilState);
    }
}

//! The PSO cache finds a description by its hash, so equal states hash alike wherever they are.
TEST(PipelineVariantTest, EqualRenderStatesHashAlike)
{
    const RHI::RenderStates a = TranslucentBlending();
    const RHI::RenderStates b = TranslucentBlending();
    EXPECT_EQ(a.GetHash(), b.GetHash());
    EXPECT_NE(a.GetHash(), BackCulling().GetHash());
}
