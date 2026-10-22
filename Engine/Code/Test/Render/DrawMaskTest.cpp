#include <gtest/gtest.h>

#include <RHI/Context/RHIContext.h>

#include <Drawable/DrawMask.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    constexpr DrawMask Opaque      = DrawMaskOf<OpaqueTag>();
    constexpr DrawMask Caster      = DrawMaskOf<ShadowCasterTag>();
    constexpr DrawMask Translucent = DrawMaskOf<TranslucentTag>();
    constexpr DrawMask Both        = DrawMaskOf<OpaqueTag, ShadowCasterTag>();
}

TEST(DrawMaskTest, EachTagHasABitOfItsOwn)
{
    EXPECT_NE(Opaque, 0u);
    EXPECT_NE(Caster, 0u);
    EXPECT_NE(Translucent, 0u);
    EXPECT_EQ(Opaque & Caster, 0u);
    EXPECT_EQ((Opaque | Caster) & Translucent, 0u);
    EXPECT_EQ(Both, Opaque | Caster);
    EXPECT_EQ(DrawMaskOf<>(), 0u);
}

TEST(DrawMaskTest, OnlyTheTagsTheCpuCullsAreCpuCulled)
{
    EXPECT_EQ(CpuCulledDrawMask, Translucent);
}

//! The two paths agree only if a spec gets exactly the tags its mask names.
TEST(DrawMaskTest, ASpecGetsTheTagsItsMaskNames)
{
    RHI::RHIContext rhi;

    const RHI::RHIHandle opaque = rhi.CreateEntity();
    AddDrawTags(rhi, opaque, Opaque);
    EXPECT_TRUE(rhi.Has<OpaqueTag>(opaque));
    EXPECT_FALSE(rhi.Has<ShadowCasterTag>(opaque));

    const RHI::RHIHandle caster = rhi.CreateEntity();
    AddDrawTags(rhi, caster, Caster);
    EXPECT_FALSE(rhi.Has<OpaqueTag>(caster));
    EXPECT_TRUE(rhi.Has<ShadowCasterTag>(caster));

    const RHI::RHIHandle translucent = rhi.CreateEntity();
    AddDrawTags(rhi, translucent, Translucent);
    EXPECT_FALSE(rhi.Has<OpaqueTag>(translucent));
    EXPECT_TRUE(rhi.Has<TranslucentTag>(translucent));

    const RHI::RHIHandle none = rhi.CreateEntity();
    AddDrawTags(rhi, none, 0);
    EXPECT_FALSE(rhi.Has<OpaqueTag>(none));
    EXPECT_FALSE(rhi.Has<ShadowCasterTag>(none));
    EXPECT_FALSE(rhi.Has<TranslucentTag>(none));
}

namespace
{
    DrawMask DrawListsOfAlphaMode(Resource::AlphaMode mode)
    {
        Resource::MaterialState state;
        state.m_alphaMode = mode;
        return DrawListsOf(state);
    }
}

TEST(DrawMaskTest, AnOpaqueSurfaceIsOpaqueAndCasts)
{
    EXPECT_EQ(DrawListsOfAlphaMode(Resource::AlphaMode::Opaque), Both);
    EXPECT_EQ(DrawListsOf(Resource::MaterialState{}), Both);
}

//! Until it clips, a masked surface draws as an opaque one.
TEST(DrawMaskTest, AMaskedSurfaceIsOpaqueAndCasts)
{
    EXPECT_EQ(DrawListsOfAlphaMode(Resource::AlphaMode::Mask), Both);
}

TEST(DrawMaskTest, ABlendedSurfaceIsTranslucentAndCastsNoShadow)
{
    EXPECT_EQ(DrawListsOfAlphaMode(Resource::AlphaMode::Blend), Translucent);
}

TEST(DrawMaskTest, EveryTranslucentModeIsTranslucentAndCastsNoShadow)
{
    EXPECT_EQ(DrawListsOfAlphaMode(Resource::AlphaMode::Additive), Translucent);
    EXPECT_EQ(DrawListsOfAlphaMode(Resource::AlphaMode::AlphaComposite), Translucent);
}

//! Double-sidedness picks a PSO, not a list.
TEST(DrawMaskTest, DoubleSidedLeavesTheListsAlone)
{
    Resource::MaterialState state;
    state.m_doubleSided = true;
    EXPECT_EQ(DrawListsOf(state), Both);
    state.m_alphaMode = Resource::AlphaMode::Blend;
    EXPECT_EQ(DrawListsOf(state), Translucent);
}
