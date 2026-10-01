#include <gtest/gtest.h>

#include <RHI/Device/DeviceLimits.h>
#include <RHI/Format.h>
#include <RHI/Resource/Image/ImageSubResource.h>

using namespace Spark;
using namespace Spark::RHI;

namespace
{
    DeviceLimits LimitsWithOffsetAlignment(uint32_t alignment)
    {
        DeviceLimits limits;
        limits.m_imageCopyOffsetAlignment = alignment;
        return limits;
    }
}

TEST(ImageCopyAlignmentTest, ADeviceAlignmentCoveringTheTexelBlockIsKept)
{
    const DeviceLimits limits = LimitsWithOffsetAlignment(512);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::R8G8B8A8_UNORM, limits), 512u);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::BC7_UNORM, limits), 512u);
}

TEST(ImageCopyAlignmentTest, ATightDeviceStillStartsOnATexelBlock)
{
    const DeviceLimits limits = LimitsWithOffsetAlignment(1);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::R8_UNORM, limits), 1u);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::R8G8B8A8_UNORM, limits), 4u);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::BC1_UNORM, limits), 8u);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::BC7_UNORM, limits), 16u);
}

TEST(ImageCopyAlignmentTest, ATexelBlockThatIsNotAPowerOfTwoWidensTheAlignment)
{
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::R32G32B32_FLOAT, LimitsWithOffsetAlignment(512)), 1536u);
    EXPECT_EQ(GetImageCopyOffsetAlignment(Format::R32G32B32_FLOAT, LimitsWithOffsetAlignment(4)), 12u);
}
