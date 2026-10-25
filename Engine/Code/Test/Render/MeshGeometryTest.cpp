#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>
#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>

#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Context/RHIContext.h>

#include <Mesh/Components.h>

#include <Geometry/MeshGeometry.h>
#include <Binding/Geometry/GeometryBindingSystem.h>

#include "FakeDevice.h"

using namespace Spark;
using namespace Spark::Render;
using Spark::Test::FakeDevice;

//! What happens to a MeshGeometry when its MeshComponent changes or goes, and the order its
//! upload goes in — it touches nothing but the contexts and the pool's allocator, so no GPU is
//! needed. Making one needs the pool, and is MeshGeometrySystem's.
class MeshGeometryTest : public ::testing::Test
{
protected:
    static constexpr uint32_t kFrameCountMax = 2;

    Ptr<RHI::PhysicalDevice> physicalDevice{ new RHI::PhysicalDevice() };
    FakeDevice               device;
    Ptr<Suballocator>        allocator;
    WorldContext             world;
    RHI::RHIContext          rhi;

    void SetUp() override
    {
        RHI::DeviceDescriptor descriptor;
        descriptor.m_frameCountMax = kFrameCountMax;
        ASSERT_EQ(device.Init(*physicalDevice, descriptor), RHI::ResultCode::Success);
        allocator = new Suballocator(device, 1024 * 1024);

        WorldExecuteContext::Push(world);
        RHI::RHIExecuteContext::Push(rhi);
    }

    void TearDown() override
    {
        RHI::RHIExecuteContext::Pop();
        WorldExecuteContext::Pop();

        // The ranges still held are handed back while the allocator can take them.
        world.Clear<MeshGeometry>();
        device.FlushReleaseQueue();
        device.BusDisconnect();
    }

    //! A mesh entity whose geometry was made from what its component names.
    Entity MeshWithGeometry()
    {
        Mesh::MeshComponent mesh;
        mesh.m_modelAssetId   = Resource::AssetId::Of<Resource::ModelAsset>("test://Model/A.glb");
        mesh.m_meshIndex      = 1;
        mesh.m_primitiveIndex = 2;

        MeshGeometry geometry;
        geometry.m_modelAssetId   = mesh.m_modelAssetId;
        geometry.m_meshIndex      = mesh.m_meshIndex;
        geometry.m_primitiveIndex = mesh.m_primitiveIndex;
        geometry.m_vertices       = allocator->Allocate(480, 48);
        geometry.m_indices        = allocator->Allocate(144, 4);

        const Entity e = world.CreateEntity();
        world.Add<Mesh::MeshComponent>(e, mesh);
        world.Add<MeshGeometry>(e, eastl::move(geometry));
        world.Add<MeshGeometryUpload>(e);
        world.Add<MeshGeometryReadyTag>(e);
        return e;
    }

    //! An entity with an upload of `byteCount` bytes, `uploadedBytes` of them in.
    Entity Upload(uint64_t byteCount, uint64_t uploadedBytes = 0)
    {
        MeshGeometryUpload upload;
        upload.m_vertexByteCount = byteCount;
        upload.m_uploadedBytes   = uploadedBytes;

        const Entity e = world.CreateEntity();
        world.Add<MeshGeometryUpload>(e, upload);
        return e;
    }

    //! One frame of uploads under `budget`, as MeshGeometrySystem stages them: each chunk is
    //! taken in, and a finished upload taken off.
    eastl::vector<eastl::pair<Entity, uint64_t>> UploadFrame(uint64_t budget)
    {
        eastl::vector<eastl::pair<Entity, uint64_t>> chunks;
        ForEachUploadChunk(world, budget, [&](Entity e, MeshGeometryUpload& upload, uint64_t byteCount)
        {
            chunks.push_back({ e, byteCount });
            upload.m_uploadedBytes += byteCount;
            if (upload.GetRemainingBytes() == 0)
            {
                world.Remove<MeshGeometryUpload>(e);
            }
        });
        return chunks;
    }

    uint32_t HalfwayCount()
    {
        uint32_t count = 0;
        world.GetView<MeshGeometryUpload>().each([&](Entity, const MeshGeometryUpload& upload)
        {
            if (upload.m_uploadedBytes > 0)
            {
                ++count;
            }
        });
        return count;
    }

    static void EndFrames(uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
        }
    }
};

TEST_F(MeshGeometryTest, UnchangedComponentKeepsItsGeometry)
{
    const Entity e = MeshWithGeometry();

    RemoveStaleMeshGeometry();

    EXPECT_TRUE(world.Has<MeshGeometry>(e));
    EXPECT_TRUE(world.Has<MeshGeometryUpload>(e));
    EXPECT_TRUE(world.Has<MeshGeometryReadyTag>(e));
}

TEST_F(MeshGeometryTest, RemovedComponentTakesItsGeometryAlong)
{
    const Entity e = MeshWithGeometry();

    world.Remove<Mesh::MeshComponent>(e);
    RemoveStaleMeshGeometry();

    EXPECT_FALSE(world.Has<MeshGeometry>(e));
    EXPECT_FALSE(world.Has<MeshGeometryUpload>(e));
    EXPECT_FALSE(world.Has<MeshGeometryReadyTag>(e));
}

TEST_F(MeshGeometryTest, ComponentNamingAnotherPrimitiveDropsItsGeometry)
{
    const Entity e = MeshWithGeometry();

    world.Get<Mesh::MeshComponent>(e).m_primitiveIndex = 3;
    RemoveStaleMeshGeometry();

    EXPECT_FALSE(world.Has<MeshGeometry>(e));
    EXPECT_FALSE(world.Has<MeshGeometryUpload>(e));
}

TEST_F(MeshGeometryTest, ComponentNamingAnotherModelDropsItsGeometry)
{
    const Entity e = MeshWithGeometry();

    world.Get<Mesh::MeshComponent>(e).m_modelAssetId =
        Resource::AssetId::Of<Resource::ModelAsset>("test://Model/B.glb");
    RemoveStaleMeshGeometry();

    EXPECT_FALSE(world.Has<MeshGeometry>(e));
}

//! A destroyed entity's ranges go back to the pool, once the frames in flight are over.
TEST_F(MeshGeometryTest, DestroyedEntityHandsItsRangesBack)
{
    const Entity e = MeshWithGeometry();
    ASSERT_GT(allocator->GetExtent(), 0u);

    world.DestoryEntity(e);
    EXPECT_GT(allocator->GetExtent(), 0u);

    EndFrames(kFrameCountMax + 1);
    EXPECT_EQ(allocator->GetExtent(), 0u);
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

TEST_F(MeshGeometryTest, AMeshIsEncodedWithTheBoxAroundIt)
{
    MeshGeometry geometry;
    geometry.m_localBounds.min = Math::Vector3(-1.0f, 0.0f, 2.0f);
    geometry.m_localBounds.max = Math::Vector3(3.0f, 4.0f, 8.0f);

    const GeometryData data = EncodeGeometryData(geometry);

    EXPECT_EQ(data.m_boundsCenter, Math::Vector3(1.0f, 2.0f, 5.0f));
    EXPECT_EQ(data.m_boundsExtents, Math::Vector3(2.0f, 2.0f, 3.0f));
}

//! An upload already begun goes on before any new one starts.
TEST_F(MeshGeometryTest, ABegunUploadGoesFirst)
{
    Upload(100);
    const Entity begun = Upload(100, 40);
    Upload(100);

    const auto chunks = UploadFrame(70);

    ASSERT_FALSE(chunks.empty());
    EXPECT_EQ(chunks[0].first, begun);
    EXPECT_EQ(chunks[0].second, 60u);
}

//! The budget is spent whole, a new upload begun only once the ones before it are through.
TEST_F(MeshGeometryTest, TheBudgetIsSpentWholeAndAtMostOneIsLeftHalfway)
{
    for (int i = 0; i < 4; ++i)
    {
        Upload(30);
    }

    // 120 bytes at 45 a frame: 45, 45, then the last 30.
    const uint64_t expected[] = { 45, 45, 30 };
    for (uint64_t frameBytes : expected)
    {
        uint64_t spent = 0;
        for (const auto& chunk : UploadFrame(45))
        {
            spent += chunk.second;
        }
        EXPECT_EQ(spent, frameBytes);
        EXPECT_LE(HalfwayCount(), 1u);
    }
    EXPECT_TRUE(world.GetView<MeshGeometryUpload>().empty());
}

TEST_F(MeshGeometryTest, NoBudgetTakesNothing)
{
    Upload(100);

    EXPECT_TRUE(UploadFrame(0).empty());
}
