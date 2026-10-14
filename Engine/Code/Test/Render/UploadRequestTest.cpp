#include <gtest/gtest.h>

#include <RHI/ResourceBuilder.h>

using namespace Spark;

//! What an upload request leaves in the two contexts when it is made and when it is over.
//! AsyncUploadSystem taking it through its states needs a copy queue, so that is not here.
class UploadRequestTest : public ::testing::Test
{
protected:
    RHI::RHIContext      rhi;
    RHI::RHIEventContext events;

    void SetUp() override
    {
        RHI::RHIExecuteContext::Push(rhi);
        RHI::RHIEventExecuteContext::Push(events);
    }

    void TearDown() override
    {
        RHI::RHIEventExecuteContext::Pop();
        RHI::RHIExecuteContext::Pop();
    }

    eastl::vector<RHI::RHIEvent> Requests()
    {
        eastl::vector<RHI::RHIEvent> requests;
        events.GetView<RHI::UploadRequestedTag>().each(
            [&](RHI::RHIEvent request) { requests.push_back(request); });
        return requests;
    }

    uint32_t PendingCount(RHI::RHIHandle resource)
    {
        const auto* pending = rhi.TryGet<RHI::PendingUploads>(resource);
        return pending ? pending->m_count : 0;
    }

    uint32_t AcquireCount(RHI::RHIHandle resource)
    {
        const auto* pending = rhi.TryGet<RHI::PendingAcquires>(resource);
        return pending ? pending->m_count : 0;
    }
};

TEST_F(UploadRequestTest, BufferRequestStartsRequestedAndNamesItsTarget)
{
    const RHI::RHIHandle buffer = rhi.CreateEntity();
    const uint32_t data[4] = { 1, 2, 3, 4 };

    RHI::RequestBufferUpload(rhi, buffer, data, sizeof(data), 16);

    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 1u);
    const auto& upload = events.Get<RHI::BufferUploadRequest>(requests[0]);
    EXPECT_EQ(upload.m_target, buffer);
    EXPECT_EQ(upload.m_data, data);
    EXPECT_EQ(upload.m_dataSize, sizeof(data));
    EXPECT_EQ(upload.m_destinationOffset, 16u);
    EXPECT_FALSE(events.Has<RHI::UploadingTag>(requests[0]));
    EXPECT_EQ(PendingCount(buffer), 1u);
}

TEST_F(UploadRequestTest, ImageRequestStartsRequestedAndNamesItsTarget)
{
    const RHI::RHIHandle image = rhi.CreateEntity();
    const uint8_t data[4] = {};

    RHI::RequestImageUpload(rhi, image, data, sizeof(data));

    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(events.Get<RHI::ImageUploadRequest>(requests[0]).m_target, image);
    EXPECT_EQ(PendingCount(image), 1u);
}

TEST_F(UploadRequestTest, AResourceWithNoUploadCarriesNoCount)
{
    const RHI::RHIHandle buffer = rhi.CreateEntity();

    EXPECT_FALSE(rhi.Has<RHI::PendingUploads>(buffer));
}

TEST_F(UploadRequestTest, AResourceCountsEachOfItsRequests)
{
    const RHI::RHIHandle buffer = rhi.CreateEntity();
    const uint32_t data[2] = {};

    RHI::RequestBufferUpload(rhi, buffer, &data[0], sizeof(uint32_t), 0);
    RHI::RequestBufferUpload(rhi, buffer, &data[1], sizeof(uint32_t), 4);

    EXPECT_EQ(Requests().size(), 2u);
    EXPECT_EQ(PendingCount(buffer), 2u);
}

//! The count goes with the last request, not with the first to finish.
TEST_F(UploadRequestTest, TheCountGoesWithTheLastRequest)
{
    const RHI::RHIHandle buffer = rhi.CreateEntity();
    const uint32_t data[2] = {};
    RHI::RequestBufferUpload(rhi, buffer, &data[0], sizeof(uint32_t), 0);
    RHI::RequestBufferUpload(rhi, buffer, &data[1], sizeof(uint32_t), 4);
    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 2u);

    RHI::FinishUploadRequest(rhi, events, requests[0]);

    EXPECT_FALSE(events.Valid(requests[0]));
    EXPECT_TRUE(events.Valid(requests[1]));
    EXPECT_EQ(PendingCount(buffer), 1u);

    RHI::FinishUploadRequest(rhi, events, requests[1]);

    EXPECT_FALSE(events.Valid(requests[1]));
    EXPECT_FALSE(rhi.Has<RHI::PendingUploads>(buffer));
}

TEST_F(UploadRequestTest, FinishingOneResourceLeavesAnother)
{
    const RHI::RHIHandle first  = rhi.CreateEntity();
    const RHI::RHIHandle second = rhi.CreateEntity();
    const uint32_t data = 0;
    RHI::RequestBufferUpload(rhi, first, &data, sizeof(data));
    RHI::RequestBufferUpload(rhi, second, &data, sizeof(data));

    for (RHI::RHIEvent request : Requests())
    {
        if (events.Get<RHI::BufferUploadRequest>(request).m_target == first)
        {
            RHI::FinishUploadRequest(rhi, events, request);
        }
    }

    EXPECT_FALSE(rhi.Has<RHI::PendingUploads>(first));
    EXPECT_EQ(PendingCount(second), 1u);
}

TEST_F(UploadRequestTest, ARequestOutlivingItsTargetStillEnds)
{
    const RHI::RHIHandle image = rhi.CreateEntity();
    const uint8_t data[4] = {};
    RHI::RequestImageUpload(rhi, image, data, sizeof(data));
    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 1u);

    rhi.DestoryEntity(image);
    RHI::FinishUploadRequest(rhi, events, requests[0]);

    EXPECT_FALSE(events.Valid(requests[0]));
}

//! Once copied, the upload is over and its acquire is not: the request stays.
TEST_F(UploadRequestTest, ACopiedUploadCountsAsAnAcquire)
{
    const RHI::RHIHandle buffer = rhi.CreateEntity();
    const uint32_t data[2] = {};
    RHI::RequestBufferUpload(rhi, buffer, &data[0], sizeof(uint32_t), 0);
    RHI::RequestBufferUpload(rhi, buffer, &data[1], sizeof(uint32_t), 4);
    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 2u);

    RHI::FinishUploadCopy(rhi, events, requests[0]);

    EXPECT_TRUE(events.Valid(requests[0]));
    EXPECT_EQ(PendingCount(buffer), 1u);
    EXPECT_EQ(AcquireCount(buffer), 1u);

    RHI::FinishUploadCopy(rhi, events, requests[1]);

    EXPECT_FALSE(rhi.Has<RHI::PendingUploads>(buffer));
    EXPECT_EQ(AcquireCount(buffer), 2u);
}

TEST_F(UploadRequestTest, TheAcquireCountGoesWithTheLastAcquire)
{
    const RHI::RHIHandle image = rhi.CreateEntity();
    const uint8_t data[4] = {};
    RHI::RequestImageUpload(rhi, image, data, sizeof(data));
    RHI::RequestImageUpload(rhi, image, data, sizeof(data));
    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 2u);
    RHI::FinishUploadCopy(rhi, events, requests[0]);
    RHI::FinishUploadCopy(rhi, events, requests[1]);

    RHI::FinishUploadAcquire(rhi, events, requests[0]);

    EXPECT_FALSE(events.Valid(requests[0]));
    EXPECT_EQ(AcquireCount(image), 1u);

    RHI::FinishUploadAcquire(rhi, events, requests[1]);

    EXPECT_FALSE(events.Valid(requests[1]));
    EXPECT_FALSE(rhi.Has<RHI::PendingAcquires>(image));
}

TEST_F(UploadRequestTest, AnAcquireOutlivingItsTargetStillEnds)
{
    const RHI::RHIHandle buffer = rhi.CreateEntity();
    const uint32_t data = 0;
    RHI::RequestBufferUpload(rhi, buffer, &data, sizeof(data));
    const auto requests = Requests();
    ASSERT_EQ(requests.size(), 1u);

    rhi.DestoryEntity(buffer);
    RHI::FinishUploadCopy(rhi, events, requests[0]);
    RHI::FinishUploadAcquire(rhi, events, requests[0]);

    EXPECT_FALSE(events.Valid(requests[0]));
}
