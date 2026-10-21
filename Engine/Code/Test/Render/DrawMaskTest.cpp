#include <gtest/gtest.h>

#include <ECS/WorldContext.h>

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

TEST(DrawMaskTest, AMeshIsOpaqueAndCastsToday)
{
    WorldContext world;
    const Entity e = world.CreateEntity();

    EXPECT_EQ(ClassifyDraw(world, e), Both);
}
