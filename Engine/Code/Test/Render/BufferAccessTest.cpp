#include <gtest/gtest.h>

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Resource/AccessFlags.h>

using namespace Spark;

TEST(BufferAccessTest, IndirectArgumentsAreAnIndirectRead)
{
    EXPECT_EQ(
        RHI::ConvertBufferAccess(RHI::AttachmentUsage::Indirect, RHI::AttachmentAccess::Read),
        RHI::AccessFlags::IndirectRead);
}

// What keeps the Scope that writes the records and the one that makes the call apart: the
// builder rejects conflicting accesses of one resource in one Scope.
TEST(BufferAccessTest, WritingABufferConflictsWithReadingItAsIndirectArguments)
{
    const RHI::AccessFlags write =
        RHI::ConvertBufferAccess(RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Write);
    const RHI::AccessFlags read =
        RHI::ConvertBufferAccess(RHI::AttachmentUsage::Shader, RHI::AttachmentAccess::Read);
    const RHI::AccessFlags indirect =
        RHI::ConvertBufferAccess(RHI::AttachmentUsage::Indirect, RHI::AttachmentAccess::Read);

    EXPECT_TRUE(RHI::HasAccessConflict(write, indirect));
    EXPECT_FALSE(RHI::HasAccessConflict(read, indirect));
}
