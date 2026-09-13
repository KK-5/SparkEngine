#include <gtest/gtest.h>

#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <Handle/HandlePool.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    //! Smallest pool that exercises the base: ids are handed out lowest first and every
    //! reclamation is recorded so a test can count them.
    class TestPool final : public HandlePool<TestPool>
    {
    public:
        explicit TestPool(uint32_t capacity)
        {
            Reserve(capacity);
            for (uint32_t i = capacity; i > 0; --i)
            {
                m_free.push_back(i - 1);
            }
        }

        SharedHandle<TestPool> Allocate()
        {
            if (m_free.empty())
            {
                return SharedHandle<TestPool>{};
            }

            const uint32_t id = m_free.back();
            m_free.pop_back();
            return MakeHandle(id);
        }

        const eastl::vector<uint32_t>& Reclaimed() const { return m_reclaimed; }

    private:
        void Free(uint32_t id) override
        {
            m_reclaimed.push_back(id);
            m_free.push_back(id);
        }

        eastl::vector<uint32_t> m_free;
        eastl::vector<uint32_t> m_reclaimed;
    };

    using TestHandle = SharedHandle<TestPool>;
    using TestWeak   = WeakHandle<TestPool>;
}

TEST(HandlePoolTest, AnIdComesBackWhenItsOnlyHandleIsDestroyed)
{
    Ptr<TestPool> pool(new TestPool(2));
    {
        TestHandle handle = pool->Allocate();
        EXPECT_TRUE(handle.IsValid());
        EXPECT_EQ(handle.Get(), 0u);
        EXPECT_TRUE(pool->Reclaimed().empty());
    }

    ASSERT_EQ(pool->Reclaimed().size(), 1u);
    EXPECT_EQ(pool->Reclaimed()[0], 0u);
}

TEST(HandlePoolTest, TheIdComesBackOnceHoweverManyCopiesThereWere)
{
    Ptr<TestPool> pool(new TestPool(2));
    {
        TestHandle a = pool->Allocate();
        {
            TestHandle b = a;
            TestHandle c = b;
        }
        EXPECT_TRUE(pool->Reclaimed().empty());
    }

    ASSERT_EQ(pool->Reclaimed().size(), 1u);
    EXPECT_EQ(pool->Reclaimed()[0], 0u);
}

TEST(HandlePoolTest, MovingDoesNotReturnTheId)
{
    Ptr<TestPool> pool(new TestPool(2));
    TestHandle a = pool->Allocate();
    TestHandle b = eastl::move(a);

    EXPECT_FALSE(a.IsValid());
    EXPECT_TRUE(b.IsValid());
    EXPECT_TRUE(pool->Reclaimed().empty());
}

TEST(HandlePoolTest, AssigningOverAHandleReturnsTheOneItHeld)
{
    Ptr<TestPool> pool(new TestPool(2));
    TestHandle a = pool->Allocate();
    TestHandle b = pool->Allocate();

    a = b;

    ASSERT_EQ(pool->Reclaimed().size(), 1u);
    EXPECT_EQ(pool->Reclaimed()[0], 0u);
    EXPECT_EQ(a.Get(), 1u);
}

TEST(HandlePoolTest, AWeakHandleGoesStaleWhenTheLastOwnerGoes)
{
    Ptr<TestPool> pool(new TestPool(2));

    TestWeak weak;
    {
        TestHandle handle = pool->Allocate();
        weak = handle.Weak();
        EXPECT_TRUE(weak.IsValid());
        EXPECT_EQ(weak.Get(), handle.Get());
    }

    EXPECT_FALSE(weak.IsValid());
}

TEST(HandlePoolTest, AWeakHandleStaysStaleWhenTheIdIsHandedOutAgain)
{
    Ptr<TestPool> pool(new TestPool(1));

    TestWeak weak;
    {
        TestHandle first = pool->Allocate();
        weak = first.Weak();
    }

    TestHandle second = pool->Allocate();
    ASSERT_EQ(second.Get(), 0u);
    EXPECT_FALSE(weak.IsValid());
}

TEST(HandlePoolTest, AnExhaustedPoolHandsOutAnEmptyHandle)
{
    Ptr<TestPool> pool(new TestPool(1));
    TestHandle first  = pool->Allocate();
    TestHandle second = pool->Allocate();

    EXPECT_TRUE(first.IsValid());
    EXPECT_FALSE(second.IsValid());
    EXPECT_FALSE(second.Weak().IsValid());
}

TEST(HandlePoolTest, AHandleOutlivesThePoolsOwner)
{
    TestHandle handle;
    {
        Ptr<TestPool> pool(new TestPool(2));
        handle = pool->Allocate();
    }

    EXPECT_TRUE(handle.IsValid());
    handle.Reset();
    EXPECT_FALSE(handle.IsValid());
}
