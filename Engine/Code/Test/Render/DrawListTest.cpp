#include <gtest/gtest.h>

#include <Math/MathUtils.h>

#include <DrawList/DrawList.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    constexpr DrawMask Opaque      = DrawMaskOf<OpaqueTag>();
    constexpr DrawMask Caster      = DrawMaskOf<ShadowCasterTag>();
    constexpr DrawMask Translucent = DrawMaskOf<TranslucentTag>();

    MeshGeometry Geometry()
    {
        MeshGeometry geometry;
        geometry.m_indexCount      = 36;
        geometry.m_firstIndex      = 120;
        geometry.m_vertexOffset    = 48;
        geometry.m_localBounds.min = Math::Vector3(-1.0f, -1.0f, -1.0f);
        geometry.m_localBounds.max = Math::Vector3(1.0f, 1.0f, 1.0f);
        return geometry;
    }

    const PipelineKey Key = MakePipelineKey(0, Resource::MaterialState{});
}

//! The instances a list of the CPU's has, each as the encoding saw it.
class DrawListTest : public ::testing::Test
{
protected:
    DrawContext draws;

    size_t InstanceCount()
    {
        return draws.GetView<DrawListInstance>().size();
    }

    const DrawListInstance& OnlyInstance()
    {
        return draws.Get<DrawListInstance>(*draws.GetView<DrawListInstance>().begin());
    }
};

TEST_F(DrawListTest, AnInstanceIsListedUnderEachBitTheCpuCulls)
{
    AddDrawListInstances(draws, Translucent, 7, Key, Geometry(), Math::Matrix4X4Const::IDENTITY);

    ASSERT_EQ(InstanceCount(), 1u);
    EXPECT_EQ(OnlyInstance().m_drawMaskBitIndex, DrawMaskBit<TranslucentTag>::Index);
}

//! Those lists are their keys (DrawListKeyCounter).
TEST_F(DrawListTest, AnInstanceOfListsTheGpuCullsIsNotListed)
{
    AddDrawListInstances(draws, Opaque | Caster, 7, Key, Geometry(), Math::Matrix4X4Const::IDENTITY);
    AddDrawListInstances(draws, 0, 8, Key, Geometry(), Math::Matrix4X4Const::IDENTITY);

    EXPECT_EQ(InstanceCount(), 0u);
}

TEST_F(DrawListTest, AnInstanceCarriesWhatItsRecordDraws)
{
    Resource::MaterialState state;
    state.m_doubleSided = true;
    const PipelineKey doubleSided = MakePipelineKey(0, state);

    AddDrawListInstances(draws, Opaque | Translucent, 7, doubleSided, Geometry(), Math::Matrix4X4Const::IDENTITY);

    ASSERT_EQ(InstanceCount(), 1u);
    const DrawListInstance& instance = OnlyInstance();
    EXPECT_EQ(instance.m_position, 7u);
    EXPECT_EQ(instance.m_key, doubleSided);
    EXPECT_EQ(instance.m_indexCount, 36u);
    EXPECT_EQ(instance.m_firstIndex, 120u);
    EXPECT_EQ(instance.m_vertexOffset, 48);
}

TEST_F(DrawListTest, AnInstanceIsBoundedWhereItsModelPutsIt)
{
    Math::Matrix4X4 model = Math::Translate(Math::Matrix4X4Const::IDENTITY, Math::Vector3(10.0f, 0.0f, -5.0f));
    model = Math::Scale(model, Math::Vector3(2.0f, 3.0f, 1.0f));

    AddDrawListInstances(draws, Translucent, 7, Key, Geometry(), model);

    ASSERT_EQ(InstanceCount(), 1u);
    const Math::AABB& bounds = OnlyInstance().m_worldBounds;
    EXPECT_FLOAT_EQ(bounds.min.x, 8.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 12.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, -3.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 3.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, -6.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, -4.0f);
}

//! A box turned a quarter about y swaps its x and z extents.
TEST_F(DrawListTest, ATurnedInstanceIsBoundedAroundItsTurnedCorners)
{
    MeshGeometry geometry = Geometry();
    geometry.m_localBounds.min = Math::Vector3(-2.0f, -1.0f, -1.0f);
    geometry.m_localBounds.max = Math::Vector3(2.0f, 1.0f, 1.0f);
    const Math::Matrix4X4 model =
        Math::Rotate(Math::Matrix4X4Const::IDENTITY, Math::Radians(90.0f), Math::Vector3(0.0f, 1.0f, 0.0f));

    AddDrawListInstances(draws, Translucent, 7, Key, geometry, model);

    ASSERT_EQ(InstanceCount(), 1u);
    const Math::AABB& bounds = OnlyInstance().m_worldBounds;
    EXPECT_NEAR(bounds.min.x, -1.0f, 1e-5f);
    EXPECT_NEAR(bounds.max.x, 1.0f, 1e-5f);
    EXPECT_NEAR(bounds.min.z, -2.0f, 1e-5f);
    EXPECT_NEAR(bounds.max.z, 2.0f, 1e-5f);
}
