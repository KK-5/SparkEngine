#include <gtest/gtest.h>

#include <RHI/Resource/ResourceState.h>

using namespace Spark;

namespace
{
    using Queue = RHI::HardwareQueueClass;
    using Mask  = RHI::HardwareQueueClassMask;
}

// An upload: the Copy queue hands the resource to the queue that owns it, without being one
// of the resource's queues itself.
TEST(QueueTransferTest, TheCopyQueueHandsOverAResourceItIsNotListedIn)
{
    EXPECT_TRUE(RHI::IsQueueTransferAllowed(Mask::Graphics, Queue::Copy, Queue::Graphics));
    EXPECT_TRUE(RHI::IsQueueTransferAllowed(Mask::Graphics, Queue::Graphics, Queue::Copy));
}

TEST(QueueTransferTest, AResourceOfOneQueueDoesNotGoToAnother)
{
    EXPECT_FALSE(RHI::IsQueueTransferAllowed(Mask::Graphics, Queue::Graphics, Queue::Compute));
    EXPECT_FALSE(RHI::IsQueueTransferAllowed(Mask::Graphics, Queue::Copy, Queue::Compute));
}

TEST(QueueTransferTest, ASharedResourceGoesBetweenItsQueues)
{
    EXPECT_TRUE(RHI::IsQueueTransferAllowed(Mask::Graphics | Mask::Compute, Queue::Graphics, Queue::Compute));
    EXPECT_TRUE(RHI::IsQueueTransferAllowed(Mask::All, Queue::Compute, Queue::Graphics));
}

TEST(QueueTransferTest, StayingOnAQueueIsNoTransfer)
{
    EXPECT_FALSE(RHI::IsQueueTransferAllowed(Mask::All, Queue::Graphics, Queue::Graphics));
    EXPECT_FALSE(RHI::IsQueueTransferAllowed(Mask::All, Queue::Copy, Queue::Copy));
}
