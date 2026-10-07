#include <gtest/gtest.h>

#include <CoreComponents/Tags.h>
#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>

#include <RHI/Context/RHIContext.h>

#include <Mesh/Components.h>

#include <Drawable/GeometrySpec.h>
#include <Geometry/MeshGeometry.h>
#include <Binding/Geometry/GeometryBindingSystem.h>

using namespace Spark;
using namespace Spark::Render;

//! What happens to a MeshGeometry when its MeshComponent changes or goes — it touches
//! nothing but the two contexts, so no device is needed. Making one needs the pool, and is
//! MeshGeometrySystem's.
class MeshGeometryTest : public ::testing::Test
{
protected:
    WorldContext    world;
    RHI::RHIContext rhi;

    void SetUp() override
    {
        WorldExecuteContext::Push(world);
        RHI::RHIExecuteContext::Push(rhi);
    }

    void TearDown() override
    {
        RHI::RHIExecuteContext::Pop();
        WorldExecuteContext::Pop();
    }

    //! A composed mesh entity whose geometry was made from what its component names.
    Entity ComposedMesh()
    {
        Mesh::MeshComponent mesh;
        mesh.m_modelAssetId   = Resource::AssetId::Of<Resource::ModelAsset>("test://Model/A.glb");
        mesh.m_meshIndex      = 1;
        mesh.m_primitiveIndex = 2;

        MeshGeometry geometry;
        geometry.m_modelAssetId   = mesh.m_modelAssetId;
        geometry.m_meshIndex      = mesh.m_meshIndex;
        geometry.m_primitiveIndex = mesh.m_primitiveIndex;
        geometry.m_vertexBuffer   = RHI::UniqueRHIHandle(rhi.CreateEntity());
        geometry.m_indexBuffer    = RHI::UniqueRHIHandle(rhi.CreateEntity());

        const Entity e = world.CreateEntity();
        world.Add<Mesh::MeshComponent>(e, mesh);
        world.Add<MeshGeometry>(e, eastl::move(geometry));
        world.Add<MeshGeometryReadyTag>(e);
        world.Add<WorldComposedTag>(e);
        return e;
    }

    bool IsDead(RHI::RHIHandle h) const { return rhi.Has<DeadTag>(h); }
};

TEST_F(MeshGeometryTest, UnchangedComponentKeepsItsGeometry)
{
    const Entity e = ComposedMesh();
    const RHI::RHIHandle vb = world.Get<MeshGeometry>(e).m_vertexBuffer.Get();

    RemoveStaleMeshGeometry();

    EXPECT_TRUE(world.Has<MeshGeometry>(e));
    EXPECT_TRUE(world.Has<MeshGeometryReadyTag>(e));
    EXPECT_TRUE(world.Has<WorldComposedTag>(e));
    EXPECT_FALSE(IsDead(vb));
}

TEST_F(MeshGeometryTest, RemovedComponentTakesItsGeometryAlong)
{
    const Entity e = ComposedMesh();
    const RHI::RHIHandle vb = world.Get<MeshGeometry>(e).m_vertexBuffer.Get();
    const RHI::RHIHandle ib = world.Get<MeshGeometry>(e).m_indexBuffer.Get();

    world.Remove<Mesh::MeshComponent>(e);
    RemoveStaleMeshGeometry();

    EXPECT_FALSE(world.Has<MeshGeometry>(e));
    EXPECT_FALSE(world.Has<MeshGeometryReadyTag>(e));
    EXPECT_FALSE(world.Has<WorldComposedTag>(e));
    EXPECT_TRUE(IsDead(vb));
    EXPECT_TRUE(IsDead(ib));
}

TEST_F(MeshGeometryTest, ComponentNamingAnotherPrimitiveDropsItsGeometry)
{
    const Entity e = ComposedMesh();
    const RHI::RHIHandle vb = world.Get<MeshGeometry>(e).m_vertexBuffer.Get();
    const RHI::RHIHandle ib = world.Get<MeshGeometry>(e).m_indexBuffer.Get();

    world.Get<Mesh::MeshComponent>(e).m_primitiveIndex = 3;
    RemoveStaleMeshGeometry();

    EXPECT_FALSE(world.Has<MeshGeometry>(e));
    EXPECT_FALSE(world.Has<WorldComposedTag>(e));
    EXPECT_TRUE(IsDead(vb));
    EXPECT_TRUE(IsDead(ib));
}

TEST_F(MeshGeometryTest, ComponentNamingAnotherModelDropsItsGeometry)
{
    const Entity e = ComposedMesh();
    const RHI::RHIHandle vb = world.Get<MeshGeometry>(e).m_vertexBuffer.Get();

    world.Get<Mesh::MeshComponent>(e).m_modelAssetId =
        Resource::AssetId::Of<Resource::ModelAsset>("test://Model/B.glb");
    RemoveStaleMeshGeometry();

    EXPECT_FALSE(world.Has<MeshGeometry>(e));
    EXPECT_TRUE(IsDead(vb));
}

TEST_F(MeshGeometryTest, DestroyedEntityMarksItsBuffersDead)
{
    const Entity e = ComposedMesh();
    const RHI::RHIHandle vb = world.Get<MeshGeometry>(e).m_vertexBuffer.Get();
    const RHI::RHIHandle ib = world.Get<MeshGeometry>(e).m_indexBuffer.Get();

    world.DestoryEntity(e);

    EXPECT_TRUE(IsDead(vb));
    EXPECT_TRUE(IsDead(ib));
}

TEST_F(MeshGeometryTest, AMeshIsEncodedWithWhereItStarts)
{
    MeshGeometry geometry;
    geometry.m_indexCount   = 36;
    geometry.m_firstIndex   = 1200;
    geometry.m_vertexOffset = 77;

    const GeometryData data = EncodeGeometryData(geometry);

    EXPECT_EQ(data.m_firstIndex, 1200u);
    EXPECT_EQ(data.m_indexCount, 36u);
    EXPECT_EQ(data.m_vertexOffset, 77);
}
