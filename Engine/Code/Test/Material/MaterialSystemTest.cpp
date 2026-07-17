#include <gtest/gtest.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>

#include <Material/MaterialSystem.h>
#include <Material/MaterialUtils.h>

using namespace Spark;
using namespace Spark::Material;

// =============================================================================
// Raw MaterialContext — the data layer with no system around it. CreateMaterial,
// handle semantics (validity / ABA), and the default-material discovery helper.
// =============================================================================
class MaterialContextTest : public ::testing::Test
{
protected:
    MaterialContext mc;
};

TEST_F(MaterialContextTest, CreateMaterialGivesValidHandleWithStoredParams)
{
    MaterialParams params{};
    params.m_metallic = 1.0f;
    params.m_roughness = 0.25f;

    const MaterialHandle h = CreateMaterial(mc, params);

    ASSERT_TRUE(mc.Valid(h));
    ASSERT_TRUE(mc.Has<MaterialParams>(h));
    EXPECT_FLOAT_EQ(mc.Get<MaterialParams>(h).m_metallic, 1.0f);
    EXPECT_FLOAT_EQ(mc.Get<MaterialParams>(h).m_roughness, 0.25f);
}

TEST_F(MaterialContextTest, DistinctCreatesGiveDistinctHandles)
{
    const MaterialHandle a = CreateMaterial(mc, MaterialParams{});
    const MaterialHandle b = CreateMaterial(mc, MaterialParams{});
    EXPECT_NE(a, b);
}

TEST_F(MaterialContextTest, NullMaterialIsInvalid)
{
    EXPECT_FALSE(mc.Valid(NullMaterial));
}

TEST_F(MaterialContextTest, DestroyInvalidatesHandleAndIsABASafe)
{
    const MaterialHandle h = CreateMaterial(mc, MaterialParams{});
    ASSERT_TRUE(mc.Valid(h));

    mc.DestoryEntity(h);
    EXPECT_FALSE(mc.Valid(h));

    // Recreating may recycle the same id, but the version differs — the stale
    // handle must never validate against the new material (ABA safety).
    const MaterialHandle reused = CreateMaterial(mc, MaterialParams{});
    EXPECT_TRUE(mc.Valid(reused));
    EXPECT_FALSE(mc.Valid(h));
}

TEST_F(MaterialContextTest, GetDefaultMaterialOnEmptyContextIsNull)
{
    // No DefaultMaterialTag registered yet — the helper reports NullMaterial
    // rather than a bogus handle.
    EXPECT_EQ(GetDefaultMaterial(mc), NullMaterial);
}

// =============================================================================
// MaterialSystem — lifecycle, the resident default, per-object auto-create, and
// the mark-sweep garbage collector. Everything is driven through public paths:
// OnTick() runs the GC, MaterialExecuteContext::Current() is the store the system
// pushes on Init (the same access production code uses).
// =============================================================================
class MaterialSystemTest : public ::testing::Test
{
protected:
    MaterialSystem   sys;
    WorldContext     world;
    MaterialContext* mat = nullptr;

    void SetUp() override
    {
        sys.Init();                        // pushes MaterialExecuteContext, builds default, connects buses
        WorldExecuteContext::Push(world);  // so auto-create + GC can reach the world
        mat = MaterialExecuteContext::Current();
        ASSERT_NE(mat, nullptr);
    }

    void TearDown() override
    {
        WorldExecuteContext::Pop();
        sys.Shutdown();                    // pops MaterialExecuteContext, disconnects buses
    }

    // A world object carrying an auto-created private material.
    Entity Object()
    {
        const Entity e = world.CreateEntity();
        world.Add<MaterialComponent>(e);   // fires OnComponentConstruct -> auto-create
        return e;
    }

    MaterialHandle MatOf(Entity e) { return world.Get<MaterialComponent>(e).m_material; }
    void           Gc() { sys.OnTick(0.0f); }
    bool           Alive(MaterialHandle h) { return mat->Valid(h) && mat->Has<MaterialParams>(h); }
};

// --- lifecycle / default -----------------------------------------------------

TEST_F(MaterialSystemTest, ShutdownPopsTheExecuteContext)
{
    WorldExecuteContext::Pop();            // undo SetUp's world push for a clean check
    sys.Shutdown();
    EXPECT_EQ(MaterialExecuteContext::Current(), nullptr);

    // Restore the state TearDown expects to unwind.
    sys.Init();
    WorldExecuteContext::Push(world);
    mat = MaterialExecuteContext::Current();
}

TEST_F(MaterialSystemTest, DefaultMaterialIsResidentAndTagged)
{
    const MaterialHandle def = sys.GetDefaultMaterial();
    ASSERT_TRUE(Alive(def));
    EXPECT_TRUE(mat->Has<DefaultMaterialTag>(def));
}

TEST_F(MaterialSystemTest, DefaultMaterialCarriesDefaultParams)
{
    const MaterialParams& p = mat->Get<MaterialParams>(sys.GetDefaultMaterial());
    EXPECT_FLOAT_EQ(p.m_baseColor.x, 0.8f);
    EXPECT_FLOAT_EQ(p.m_baseColor.y, 0.8f);
    EXPECT_FLOAT_EQ(p.m_baseColor.z, 0.8f);
    EXPECT_FLOAT_EQ(p.m_baseColor.w, 1.0f);
    EXPECT_FLOAT_EQ(p.m_metallic, 0.0f);
    EXPECT_FLOAT_EQ(p.m_roughness, 0.5f);
    EXPECT_FLOAT_EQ(p.m_specular, 0.5f);
}

// --- auto-create / per-object ------------------------------------------------

TEST_F(MaterialSystemTest, AddingComponentAutoCreatesAPrivateMaterial)
{
    const Entity e = Object();
    const MaterialHandle m = MatOf(e);

    EXPECT_TRUE(Alive(m));
    EXPECT_NE(m, NullMaterial);
    EXPECT_NE(m, sys.GetDefaultMaterial());   // private, not the shared default
}

TEST_F(MaterialSystemTest, AutoCreateSeedsFromCurrentDefaultParams)
{
    // Edit the default before adding the object — the new private material copies
    // the default's *current* params, not the compile-time defaults.
    mat->Get<MaterialParams>(sys.GetDefaultMaterial()).m_roughness = 0.9f;

    const MaterialHandle m = MatOf(Object());
    EXPECT_FLOAT_EQ(mat->Get<MaterialParams>(m).m_roughness, 0.9f);
}

TEST_F(MaterialSystemTest, PerObjectMaterialsAreIndependentCopies)
{
    const MaterialHandle a = MatOf(Object());
    const MaterialHandle b = MatOf(Object());
    ASSERT_NE(a, b);

    mat->Get<MaterialParams>(a).m_metallic = 1.0f;

    // Editing a's copy touches neither b nor the default.
    EXPECT_FLOAT_EQ(mat->Get<MaterialParams>(b).m_metallic, 0.0f);
    EXPECT_FLOAT_EQ(mat->Get<MaterialParams>(sys.GetDefaultMaterial()).m_metallic, 0.0f);
}

TEST_F(MaterialSystemTest, PreSetMaterialIsNotOverwrittenOnAdd)
{
    const MaterialHandle h = CreateMaterial(*mat, MaterialParams{});

    const Entity e = world.CreateEntity();
    world.Add<MaterialComponent>(e, MaterialComponent{h});   // already carries a material

    EXPECT_EQ(MatOf(e), h);   // auto-create leaves an explicit reference untouched
}

// --- garbage collection ------------------------------------------------------

TEST_F(MaterialSystemTest, GcPinsTheDefaultWithNoReferences)
{
    Gc();
    EXPECT_TRUE(Alive(sys.GetDefaultMaterial()));
}

TEST_F(MaterialSystemTest, GcKeepsReferencedMaterialAlive)
{
    const MaterialHandle m = MatOf(Object());
    Gc();
    EXPECT_TRUE(Alive(m));
}

TEST_F(MaterialSystemTest, GcReclaimsPrivateMaterialWhenEntityDestroyed)
{
    const Entity e = Object();
    const MaterialHandle m = MatOf(e);
    Gc();
    ASSERT_TRUE(Alive(m));

    world.DestoryEntity(e);
    Gc();
    EXPECT_FALSE(Alive(m));
    EXPECT_FALSE(mat->Valid(m));   // handle fully invalidated
}

TEST_F(MaterialSystemTest, GcReclaimsOrphanNeverAssignedToAComponent)
{
    // Known boundary: a material held only by a local handle, never referenced by
    // any MaterialComponent, is not reachable from a root and gets collected.
    const MaterialHandle h = CreateMaterial(*mat, MaterialParams{});
    ASSERT_TRUE(Alive(h));

    Gc();
    EXPECT_FALSE(Alive(h));
}

TEST_F(MaterialSystemTest, SharedMaterialSurvivesUntilLastReferrerGone)
{
    const Entity eA = Object();
    const Entity eB = Object();
    const MaterialHandle mA = MatOf(eA);
    const MaterialHandle mB = MatOf(eB);
    ASSERT_NE(mA, mB);

    // B adopts A's material via a raw in-place field write — deliberately no event
    // fires. This is exactly the case a refcount could not catch; mark-sweep only
    // looks at what is actually referenced now.
    world.Get<MaterialComponent>(eB).m_material = mA;

    Gc();
    EXPECT_TRUE(Alive(mA));    // shared, still referenced by both
    EXPECT_FALSE(Alive(mB));   // B's original private material is now orphaned

    world.DestoryEntity(eA);
    Gc();
    EXPECT_TRUE(Alive(mA));    // eB still references it

    world.DestoryEntity(eB);
    Gc();
    EXPECT_FALSE(Alive(mA));   // no referrers left
}
