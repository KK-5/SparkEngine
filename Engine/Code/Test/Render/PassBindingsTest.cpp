#include <gtest/gtest.h>

#include <Pass/PassAccess.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    // No backend allocation: observe when the final owning component releases the object.
    class TrackedBindings final : public RHI::ShaderBindings
    {
    public:
        explicit TrackedBindings(uint32_t& shutdowns) : m_shutdowns(shutdowns) {}

    private:
        RHI::ResultCode InitInternal(RHI::Device&, const Descriptor&) override
        {
            return RHI::ResultCode::Success;
        }

        void ShutdownInternal() override { ++m_shutdowns; }

        uint32_t& m_shutdowns;
    };
}

TEST(PassBindingsTest, ThePassOwnsBindingsWithoutAnRHIEntity)
{
    uint32_t shutdowns = 0;
    TrackedBindings bindings(shutdowns);
    PassContext context;
    const Pass pass = context.CreatePass();

    context.Add<PassBindings>(pass, PassBindings{ Ptr<RHI::ShaderBindings>(&bindings) });
    EXPECT_EQ(context.Get<PassBindings>(pass).m_bindings.get(), &bindings);
    EXPECT_EQ(shutdowns, 0u);

    context.DestoryEntity(pass);
    EXPECT_EQ(shutdowns, 1u);
}

TEST(PassBindingsTest, ReleasingOneContextLeavesAnotherContextsBindingsAlive)
{
    uint32_t firstShutdowns = 0;
    uint32_t secondShutdowns = 0;
    TrackedBindings first(firstShutdowns);
    TrackedBindings second(secondShutdowns);
    PassContext firstContext;
    PassContext secondContext;
    const Pass firstPass = firstContext.CreatePass();
    const Pass secondPass = secondContext.CreatePass();
    firstContext.Add<PassBindings>(firstPass, PassBindings{ Ptr<RHI::ShaderBindings>(&first) });
    secondContext.Add<PassBindings>(secondPass, PassBindings{ Ptr<RHI::ShaderBindings>(&second) });

    ReleasePassBindings(firstContext);
    EXPECT_FALSE(firstContext.Get<PassBindings>(firstPass).m_bindings);
    EXPECT_EQ(firstShutdowns, 1u);
    EXPECT_EQ(secondShutdowns, 0u);
    EXPECT_EQ(secondContext.Get<PassBindings>(secondPass).m_bindings.get(), &second);

    ReleasePassBindings(firstContext);
    EXPECT_EQ(firstShutdowns, 1u);

    ReleasePassBindings(secondContext);
    EXPECT_EQ(secondShutdowns, 1u);
}

TEST(PassBindingsTest, ASharedObjectLivesUntilItsLastPassReleasesIt)
{
    uint32_t shutdowns = 0;
    TrackedBindings bindings(shutdowns);
    PassContext context;
    const Pass first = context.CreatePass();
    const Pass second = context.CreatePass();
    context.Add<PassBindings>(first, PassBindings{ Ptr<RHI::ShaderBindings>(&bindings) });
    context.Add<PassBindings>(second, PassBindings{ Ptr<RHI::ShaderBindings>(&bindings) });

    context.DestoryEntity(first);
    EXPECT_EQ(shutdowns, 0u);

    context.DestoryEntity(second);
    EXPECT_EQ(shutdowns, 1u);
}
