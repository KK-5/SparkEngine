#include <gtest/gtest.h>

#include <CoreComponents/Tags.h>
#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>

#include <Material/Components.h>
#include <Material/MaterialContext.h>
#include <Material/MaterialUtils.h>

#include <Binding/Material/MaterialBinding.h>

using namespace Spark;
using namespace Spark::Render;

//! Synthesis and reaping only — both touch nothing but the two contexts, so no device is
//! needed. What happens to the g_Materials slot in between is GlobalBuffer's.
class MaterialOverrideTest : public ::testing::Test
{
protected:
    WorldContext              world;
    Material::MaterialContext mat;

    void SetUp() override
    {
        WorldExecuteContext::Push(world);
        Material::MaterialExecuteContext::Push(mat);
    }

    void TearDown() override
    {
        Material::MaterialExecuteContext::Pop();
        WorldExecuteContext::Pop();
    }

    Entity ObjectWithOverride(float metallic)
    {
        const Entity e = world.CreateEntity();
        Material::StandardPBROverride params;
        params.m_metallic = metallic;
        world.Add<Material::StandardPBROverride>(e, params);
        return e;
    }

    Material::MaterialHandle MaterialOf(Entity e)
    {
        const auto* ref = world.TryGet<MaterialOverrideRef>(e);
        return ref ? ref->m_material : Material::NullMaterial;
    }

    size_t LiveMaterials()
    {
        size_t n = 0;
        for (auto h : mat.GetView<Resource::StandardPBR>())
        {
            (void)h;
            ++n;
        }
        return n;
    }
};

TEST_F(MaterialOverrideTest, AnOverrideGetsAMaterialOfItsOwn)
{
    const Entity e = ObjectWithOverride(0.25f);

    SyncOverrideMaterials();

    const Material::MaterialHandle h = MaterialOf(e);
    ASSERT_NE(h, Material::NullMaterial);
    ASSERT_TRUE(mat.Valid(h));
    EXPECT_FLOAT_EQ(mat.Get<Resource::StandardPBR>(h).m_metallic, 0.25f);
}

TEST_F(MaterialOverrideTest, SyncingAgainReusesTheSameMaterial)
{
    const Entity e = ObjectWithOverride(0.25f);

    SyncOverrideMaterials();
    const Material::MaterialHandle first = MaterialOf(e);
    SyncOverrideMaterials();

    EXPECT_EQ(MaterialOf(e), first);
    EXPECT_EQ(LiveMaterials(), 1u);
}

TEST_F(MaterialOverrideTest, EditingTheOverrideReachesItsMaterial)
{
    const Entity e = ObjectWithOverride(0.25f);
    SyncOverrideMaterials();

    world.Get<Material::StandardPBROverride>(e).m_metallic = 0.75f;
    SyncOverrideMaterials();

    EXPECT_FLOAT_EQ(mat.Get<Resource::StandardPBR>(MaterialOf(e)).m_metallic, 0.75f);
}

TEST_F(MaterialOverrideTest, TwoObjectsDoNotShareAMaterial)
{
    const Entity a = ObjectWithOverride(0.25f);
    const Entity b = ObjectWithOverride(0.75f);

    SyncOverrideMaterials();

    EXPECT_NE(MaterialOf(a), MaterialOf(b));
    EXPECT_EQ(LiveMaterials(), 2u);
}

TEST_F(MaterialOverrideTest, RemovingTheOverrideMarksAndDropsTheLink)
{
    const Entity e = ObjectWithOverride(0.25f);
    SyncOverrideMaterials();
    const Material::MaterialHandle h = MaterialOf(e);

    world.Remove<Material::StandardPBROverride>(e);
    SyncOverrideMaterials();

    // Marked, not destroyed: the slot is reclaimed by seeing the tag on a live entity.
    EXPECT_TRUE(mat.Has<DeadTag>(h));
    EXPECT_FALSE(world.Has<MaterialOverrideRef>(e));

    ReapDeadMaterials();
    EXPECT_FALSE(mat.Valid(h));
}

TEST_F(MaterialOverrideTest, ReapLeavesAMaterialThatIsStillReferenced)
{
    const Entity e = ObjectWithOverride(0.25f);
    SyncOverrideMaterials();

    ReapDeadMaterials();

    EXPECT_TRUE(mat.Valid(MaterialOf(e)));
}

TEST_F(MaterialOverrideTest, ASecondSyncAfterRemovalCreatesNothing)
{
    const Entity e = ObjectWithOverride(0.25f);
    SyncOverrideMaterials();

    world.Remove<Material::StandardPBROverride>(e);
    SyncOverrideMaterials();
    ReapDeadMaterials();
    SyncOverrideMaterials();

    EXPECT_EQ(LiveMaterials(), 0u);
}
