#include <gtest/gtest.h>

#include <thread>

#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Device/Device.h>
#include <RHI/Device/PhysicalDevice.h>

#include <Suballocation/Suballocator.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    constexpr uint32_t kFrameCountMax = 2;

    //! The base Device's release queue and frame hook, with no backend: what a Suballocator
    //! reaches of a device is QueueForRelease, released at OnFrameEnd.
    class FakeDevice final : public RHI::Device
    {
    private:
        RHI::ResultCode InitInternal(RHI::PhysicalDevice&) override { return RHI::ResultCode::Success; }
        void ShutdownInternal() override {}
        RHI::ResultCode InitializeLimits() override { return RHI::ResultCode::Success; }
        void FillFormatsCapabilitiesInternal(FormatCapabilitiesList&) override {}
    };

    class SuballocatorTests : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            RHI::DeviceDescriptor descriptor;
            descriptor.m_frameCountMax = kFrameCountMax;
            ASSERT_EQ(m_device.Init(*m_physicalDevice, descriptor), RHI::ResultCode::Success);
        }

        void TearDown() override
        {
            // What the tests left queued goes now, while the allocators it holds can still go.
            // Device::Shutdown, which would do both, runs only when the last Ptr goes.
            m_device.FlushReleaseQueue();
            m_device.BusDisconnect();
        }

        Ptr<Suballocator> Make(uint64_t capacity, uint64_t pageSize = 0)
        {
            return Ptr<Suballocator>(new Suballocator(m_device, capacity, pageSize));
        }

        static void EndFrames(uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
            }
        }

        //! Page by page, in page order.
        static eastl::vector<Suballocator::PageChange> TakePages(Suballocator& allocator)
        {
            eastl::vector<Suballocator::PageChange> changes;
            allocator.TakePageChanges(changes);
            eastl::sort(changes.begin(), changes.end(),
                [](const auto& a, const auto& b) { return a.m_page < b.m_page; });
            return changes;
        }

        static void ExpectPages(const eastl::vector<Suballocator::PageChange>& changes,
                                std::initializer_list<eastl::pair<uint32_t, bool>> expected)
        {
            ASSERT_EQ(changes.size(), expected.size());
            size_t i = 0;
            for (const auto& [page, occupied] : expected)
            {
                EXPECT_EQ(changes[i].m_page, page);
                EXPECT_EQ(changes[i].m_occupied, occupied) << "page " << page;
                ++i;
            }
        }

        Ptr<RHI::PhysicalDevice> m_physicalDevice{ new RHI::PhysicalDevice() };
        FakeDevice               m_device;
    };
}

TEST_F(SuballocatorTests, AllocatesInBytes)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(10);
    ASSERT_TRUE(a.IsValid());
    EXPECT_EQ(a.GetOffset(), 0u);
    EXPECT_EQ(a.GetSize(), 10u);

    Suballocation b = allocator->Allocate(5, 4);
    ASSERT_TRUE(b.IsValid());
    EXPECT_EQ(b.GetOffset() % 4, 0u);
    EXPECT_GE(b.GetOffset(), 10u);
    EXPECT_EQ(allocator->GetExtent(), b.GetOffset() + 5);
}

TEST_F(SuballocatorTests, EmptyWhenNothingFits)
{
    Ptr<Suballocator> allocator = Make(10);
    Suballocation all = allocator->Allocate(10);
    EXPECT_TRUE(all.IsValid());
    EXPECT_FALSE(allocator->Allocate(1).IsValid());
}

TEST_F(SuballocatorTests, HandedBackAfterTheGPU)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(10);
    a.Reset();
    EXPECT_FALSE(a.IsValid());

    // Still taken while frames that may read it are in flight.
    EndFrames(kFrameCountMax);
    EXPECT_EQ(allocator->GetExtent(), 10u);
    EndFrames(1);
    EXPECT_EQ(allocator->GetExtent(), 0u);
}

TEST_F(SuballocatorTests, NotHandedOutAgainBeforeItIsBack)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(10);
    a.Reset();
    Suballocation b = allocator->Allocate(10);
    EXPECT_EQ(b.GetOffset(), 10u);

    EndFrames(kFrameCountMax + 1);
    Suballocation c = allocator->Allocate(10);
    EXPECT_EQ(c.GetOffset(), 0u);
}

TEST_F(SuballocatorTests, MoveTransfersOwnership)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(10);
    Suballocation b = eastl::move(a);
    EXPECT_FALSE(a.IsValid());
    ASSERT_TRUE(b.IsValid());
    EXPECT_EQ(b.GetOffset(), 0u);

    // The moved-from one gives nothing back.
    a.Reset();
    EndFrames(kFrameCountMax + 1);
    EXPECT_EQ(allocator->GetExtent(), 10u);

    b.Reset();
    EndFrames(kFrameCountMax + 1);
    EXPECT_EQ(allocator->GetExtent(), 0u);
}

TEST_F(SuballocatorTests, MoveAssignmentReleasesWhatItHeld)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(10);
    Suballocation b = allocator->Allocate(10);
    a = eastl::move(b);
    EXPECT_EQ(a.GetOffset(), 10u);

    EndFrames(kFrameCountMax + 1);
    EXPECT_EQ(allocator->Allocate(10).GetOffset(), 0u);
}

TEST_F(SuballocatorTests, OutlivesItsOwner)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(10);
    allocator.reset();

    // The part and then its pending release hold it.
    a.Reset();
    EndFrames(kFrameCountMax + 1);
}

TEST_F(SuballocatorTests, ConcurrentAllocationAndRelease)
{
    static constexpr uint32_t kThreads = 8;
    static constexpr uint32_t kRounds  = 2000;
    Ptr<Suballocator> allocator = Make(1 << 20);

    eastl::vector<std::thread> threads;
    for (uint32_t t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&allocator, t]()
        {
            eastl::vector<Suballocation> held;
            for (uint32_t i = 0; i < kRounds; ++i)
            {
                held.push_back(allocator->Allocate(1 + (i + t) % 64));
                if (i % 3 == 2)
                {
                    held.erase(held.begin());
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EndFrames(kFrameCountMax + 1);
    EXPECT_EQ(allocator->GetExtent(), 0u);
    EXPECT_TRUE(allocator->Allocate(1 << 20).IsValid());
}

TEST_F(SuballocatorTests, NoPagesCountedWithoutAPageSize)
{
    Ptr<Suballocator> allocator = Make(100);
    Suballocation a = allocator->Allocate(50);
    EXPECT_TRUE(TakePages(*allocator).empty());
}

TEST_F(SuballocatorTests, EveryPageTouchedIsReportedOnce)
{
    Ptr<Suballocator> allocator = Make(64, 8);
    Suballocation a = allocator->Allocate(20);
    ExpectPages(TakePages(*allocator), { { 0, true }, { 1, true }, { 2, true } });
    EXPECT_TRUE(TakePages(*allocator).empty());

    // Page 2 is touched already.
    Suballocation b = allocator->Allocate(4);
    EXPECT_TRUE(TakePages(*allocator).empty());
}

TEST_F(SuballocatorTests, AStraddlingElementCountsOnBothPages)
{
    // Elements of 3 bytes on 8 byte pages: the third spans bytes 6 to 8.
    Ptr<Suballocator> allocator = Make(64, 8);
    Suballocation first  = allocator->Allocate(3);
    Suballocation second = allocator->Allocate(3);
    Suballocation third  = allocator->Allocate(3);
    EXPECT_EQ(third.GetOffset(), 6u);
    ExpectPages(TakePages(*allocator), { { 0, true }, { 1, true } });

    first.Reset();
    second.Reset();
    EndFrames(kFrameCountMax + 1);
    EXPECT_TRUE(TakePages(*allocator).empty());

    third.Reset();
    EndFrames(kFrameCountMax + 1);
    ExpectPages(TakePages(*allocator), { { 0, false }, { 1, false } });
}

TEST_F(SuballocatorTests, APageEmptiesOnHandBackNotOnRelease)
{
    Ptr<Suballocator> allocator = Make(64, 8);
    Suballocation a = allocator->Allocate(8);
    TakePages(*allocator);

    a.Reset();
    EXPECT_TRUE(TakePages(*allocator).empty());
    EndFrames(kFrameCountMax);
    EXPECT_TRUE(TakePages(*allocator).empty());
    EndFrames(1);
    ExpectPages(TakePages(*allocator), { { 0, false } });
}

TEST_F(SuballocatorTests, APageChangedTwiceIsReportedOnceAsItIsNow)
{
    Ptr<Suballocator> allocator = Make(64, 8);
    Suballocation a = allocator->Allocate(8);
    TakePages(*allocator);

    // Emptied, then touched again before the owner looks: it stays mapped.
    a.Reset();
    EndFrames(kFrameCountMax + 1);
    Suballocation b = allocator->Allocate(8);
    EXPECT_EQ(b.GetOffset(), 0u);
    ExpectPages(TakePages(*allocator), { { 0, true } });

    // Touched, then emptied before the owner looks: it was never mapped.
    Suballocation c = allocator->Allocate(8);
    EXPECT_EQ(c.GetOffset(), 8u);
    c.Reset();
    EndFrames(kFrameCountMax + 1);
    ExpectPages(TakePages(*allocator), { { 1, false } });
}

TEST_F(SuballocatorTests, AnArrayOfOneElementSizeStaysOnElementBoundaries)
{
    // Alignment 1: the space is only ever cut at multiples of the element, and a freed
    // element's hole is reused before the end grows.
    constexpr uint64_t kElement = 48;
    Ptr<Suballocator> allocator = Make(kElement * 1000);

    eastl::vector<Suballocation> elements;
    for (int i = 0; i < 100; ++i)
    {
        elements.push_back(allocator->Allocate(kElement));
    }
    for (int i = 1; i < 100; i += 3)
    {
        elements[i].Reset();
    }
    EndFrames(kFrameCountMax + 1);

    const uint64_t extent = allocator->GetExtent();
    for (int i = 1; i < 100; i += 3)
    {
        Suballocation element = allocator->Allocate(kElement);
        EXPECT_EQ(element.GetOffset() % kElement, 0u);
        EXPECT_LT(element.GetOffset(), extent);
        elements.push_back(eastl::move(element));
    }
    EXPECT_EQ(allocator->GetExtent(), extent);
}
