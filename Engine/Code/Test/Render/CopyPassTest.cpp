#include <gtest/gtest.h>

#include <Pass/CopyPass.h>

using namespace Spark;
using namespace Spark::Render;

//! A copy pass is a pass of its own kind: no shader, no PSO, run on the queue it names.
TEST(CopyPassTest, FinalizeRegistersACopyPass)
{
    PassContext context;
    const Pass pass = SPARK_COPY_PASS(context, "CopyPassTest.Copy")
        .Queue(RHI::HardwareQueueClass::Graphics)
        .Build([](CopyPassScopes&) {})
        .Finalize();

    EXPECT_TRUE(context.Has<CopyPassTag>(pass));
    EXPECT_FALSE(context.Has<ComputePassTag>(pass));
    EXPECT_FALSE(context.Has<RenderPassTag>(pass));
    EXPECT_TRUE(context.Has<ActivePassTag>(pass));
    EXPECT_FALSE(context.Has<PassStageFunctions>(pass));
    EXPECT_FALSE(context.Has<PassPipelineLayout>(pass));

    EXPECT_EQ(context.Get<PassExecuteQueue>(pass).m_queue, RHI::HardwareQueueClass::Graphics);
    EXPECT_EQ(context.Get<PassName>(pass).m_name, ObjectName("CopyPassTest.Copy"));

    const PassFunctions& functions = context.Get<PassFunctions>(pass);
    EXPECT_TRUE(static_cast<bool>(functions.m_buildFunction));
    EXPECT_FALSE(static_cast<bool>(functions.m_executeFunction));
}

TEST(CopyPassTest, AnInactivePassIsNotActive)
{
    PassContext context;
    const Pass pass = SPARK_COPY_PASS(context, "CopyPassTest.Inactive")
        .Queue(RHI::HardwareQueueClass::Copy)
        .Inactive()
        .Build([](CopyPassScopes&) {})
        .Finalize();

    EXPECT_TRUE(context.Has<CopyPassTag>(pass));
    EXPECT_FALSE(context.Has<ActivePassTag>(pass));
    EXPECT_EQ(context.Get<PassExecuteQueue>(pass).m_queue, RHI::HardwareQueueClass::Copy);
}
