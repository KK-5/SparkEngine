#include <gtest/gtest.h>

#include <RHI/Resource/Image/ImageSubresourceStates.h>

using namespace Spark;
using namespace Spark::RHI;

namespace
{
    using SpanList = ImageSubresourceStates::SpanList;

    const ResourceState Idle {};
    const ResourceState Sampled { AccessFlags::ShaderSampledRead, HardwareQueueClass::Graphics, AttachmentStage::ComputeShader };
    const ResourceState Storage { AccessFlags::ShaderStorageWrite, HardwareQueueClass::Graphics, AttachmentStage::ComputeShader };

    ImageSubresourceRange Mips(uint16_t mipMin, uint16_t mipMax)
    {
        return ImageSubresourceRange(mipMin, mipMax, 0, ImageSubresourceRange::HighestSliceIndex);
    }

    ImageSubresourceRange Aspect(ImageAspectFlags aspect)
    {
        ImageSubresourceRange range;
        range.m_aspectFlags = aspect;
        return range;
    }

    ImageSubresourceRange Range(
        uint16_t mipMin, uint16_t mipMax, uint16_t arrayMin, uint16_t arrayMax, ImageAspectFlags aspects)
    {
        ImageSubresourceRange range(mipMin, mipMax, arrayMin, arrayMax);
        range.m_aspectFlags = aspects;
        return range;
    }

    SpanList SpansOf(const ImageSubresourceStates& states, const ImageSubresourceRange& range = {})
    {
        SpanList spans;
        states.GetSpans(range, spans);
        return spans;
    }

    void ExpectSpan(const ImageSubresourceSpan& span, const ImageSubresourceRange& range, const ResourceState& state)
    {
        EXPECT_TRUE(span.m_range == range)
            << "mips [" << span.m_range.m_mipSliceMin << ", " << span.m_range.m_mipSliceMax
            << "] slices [" << span.m_range.m_arraySliceMin << ", " << span.m_range.m_arraySliceMax
            << "] aspects " << static_cast<uint32_t>(span.m_range.m_aspectFlags);
        EXPECT_TRUE(span.m_state == state);
    }
}

TEST(ImageSubresourceStatesTest, AWholeImageWriteKeepsOneState)
{
    ImageSubresourceStates states(4, 1, ImageAspectFlags::Color);
    EXPECT_TRUE(states.IsUniform());
    EXPECT_TRUE(states.GetUniform() == Idle);

    states.Set(ImageSubresourceRange(), Sampled);
    EXPECT_TRUE(states.IsUniform());
    EXPECT_TRUE(states.GetUniform() == Sampled);

    const SpanList spans = SpansOf(states);
    ASSERT_EQ(spans.size(), 1u);
    ExpectSpan(spans[0], Range(0, 3, 0, 0, ImageAspectFlags::Color), Sampled);
}

TEST(ImageSubresourceStatesTest, OpenEndedRangesResolveAgainstTheImage)
{
    ImageSubresourceStates states(5, 6, ImageAspectFlags::Color);

    EXPECT_TRUE(states.IsWholeImage(ImageSubresourceRange()));
    EXPECT_TRUE(states.IsWholeImage(Range(0, 4, 0, 5, ImageAspectFlags::Color)));
    EXPECT_FALSE(states.IsWholeImage(Mips(0, 3)));
    EXPECT_FALSE(states.IsWholeImage(ImageSubresourceRange(0, 4, 1, 5)));

    EXPECT_TRUE(states.Normalize(Mips(2, ImageSubresourceRange::HighestSliceIndex))
        == Range(2, 4, 0, 5, ImageAspectFlags::Color));
}

TEST(ImageSubresourceStatesTest, WritingOneMipSplitsTheImage)
{
    ImageSubresourceStates states(4, 1, ImageAspectFlags::Color);
    states.Set(Mips(1, 1), Storage);
    EXPECT_FALSE(states.IsUniform());

    const SpanList written = SpansOf(states, Mips(1, 1));
    ASSERT_EQ(written.size(), 1u);
    ExpectSpan(written[0], Range(1, 1, 0, 0, ImageAspectFlags::Color), Storage);

    const SpanList all = SpansOf(states);
    ASSERT_EQ(all.size(), 3u);
    ExpectSpan(all[0], Range(0, 0, 0, 0, ImageAspectFlags::Color), Idle);
    ExpectSpan(all[1], Range(1, 1, 0, 0, ImageAspectFlags::Color), Storage);
    ExpectSpan(all[2], Range(2, 3, 0, 0, ImageAspectFlags::Color), Idle);
}

TEST(ImageSubresourceStatesTest, WritingTheStateAlreadyThereKeepsOneState)
{
    ImageSubresourceStates states(4, 1, ImageAspectFlags::Color, Sampled);
    states.Set(Mips(1, 2), Sampled);
    EXPECT_TRUE(states.IsUniform());
}

// A mip chain generated level by level: each level is written, then read to make the next.
TEST(ImageSubresourceStatesTest, AGeneratedMipChainIsTwoSpansAndFoldsBackWhenReadWhole)
{
    const uint16_t mipLevels = 5;
    ImageSubresourceStates states(mipLevels, 1, ImageAspectFlags::Color);
    for (uint16_t mip = 0; mip < mipLevels; ++mip)
    {
        states.Set(Mips(mip, mip), Storage);
        if (mip > 0)
        {
            states.Set(Mips(mip - 1, mip - 1), Sampled);
        }
    }
    EXPECT_FALSE(states.IsUniform());

    const SpanList spans = SpansOf(states);
    ASSERT_EQ(spans.size(), 2u);
    ExpectSpan(spans[0], Range(0, 3, 0, 0, ImageAspectFlags::Color), Sampled);
    ExpectSpan(spans[1], Range(4, 4, 0, 0, ImageAspectFlags::Color), Storage);

    states.Set(ImageSubresourceRange(), Sampled);
    EXPECT_TRUE(states.IsUniform());
    EXPECT_TRUE(states.GetUniform() == Sampled);
}

TEST(ImageSubresourceStatesTest, PartialWritesThatEndUpAgreeingFoldBack)
{
    ImageSubresourceStates states(3, 2, ImageAspectFlags::Color);
    states.Set(ImageSubresourceRange(0, 2, 0, 0), Sampled);
    EXPECT_FALSE(states.IsUniform());

    states.Set(ImageSubresourceRange(0, 2, 1, 1), Sampled);
    EXPECT_TRUE(states.IsUniform());
    EXPECT_TRUE(states.GetUniform() == Sampled);
}

TEST(ImageSubresourceStatesTest, SlicesSplitTheSameWayMergeIntoRectangles)
{
    // A cube whose last mip differs on every face: two rectangles, not two per face.
    ImageSubresourceStates states(5, 6, ImageAspectFlags::Color, Sampled);
    states.Set(Mips(4, 4), Storage);

    const SpanList spans = SpansOf(states);
    ASSERT_EQ(spans.size(), 2u);
    ExpectSpan(spans[0], Range(0, 3, 0, 5, ImageAspectFlags::Color), Sampled);
    ExpectSpan(spans[1], Range(4, 4, 0, 5, ImageAspectFlags::Color), Storage);
}

TEST(ImageSubresourceStatesTest, SlicesSplitDifferentlyStayApart)
{
    ImageSubresourceStates states(3, 3, ImageAspectFlags::Color, Sampled);
    states.Set(ImageSubresourceRange(1, 1, 1, 1), Storage);

    const SpanList spans = SpansOf(states);
    ASSERT_EQ(spans.size(), 5u);
    ExpectSpan(spans[0], Range(0, 2, 0, 0, ImageAspectFlags::Color), Sampled);
    ExpectSpan(spans[1], Range(0, 0, 1, 1, ImageAspectFlags::Color), Sampled);
    ExpectSpan(spans[2], Range(1, 1, 1, 1, ImageAspectFlags::Color), Storage);
    ExpectSpan(spans[3], Range(2, 2, 1, 1, ImageAspectFlags::Color), Sampled);
    ExpectSpan(spans[4], Range(0, 2, 2, 2, ImageAspectFlags::Color), Sampled);
}

TEST(ImageSubresourceStatesTest, SpansCoverOnlyTheRangeAsked)
{
    ImageSubresourceStates states(4, 2, ImageAspectFlags::Color, Sampled);
    states.Set(ImageSubresourceRange(0, 0, 0, 0), Storage);

    const SpanList spans = SpansOf(states, ImageSubresourceRange(1, 3, 0, 1));
    ASSERT_EQ(spans.size(), 1u);
    ExpectSpan(spans[0], Range(1, 3, 0, 1, ImageAspectFlags::Color), Sampled);
}

TEST(ImageSubresourceStatesTest, DepthAndStencilHoldSeparateStates)
{
    const ResourceState depthRead  { AccessFlags::DepthStencilRead, HardwareQueueClass::Graphics, AttachmentStage::EarlyFragmentTest };
    const ResourceState depthWrite { AccessFlags::DepthStencilWrite, HardwareQueueClass::Graphics, AttachmentStage::EarlyFragmentTest };

    ImageSubresourceStates states(2, 1, ImageAspectFlags::DepthStencil, depthWrite);
    EXPECT_TRUE(states.IsWholeImage(ImageSubresourceRange()));
    EXPECT_FALSE(states.IsWholeImage(Aspect(ImageAspectFlags::Depth)));

    states.Set(Aspect(ImageAspectFlags::Depth), depthRead);
    EXPECT_FALSE(states.IsUniform());

    const SpanList split = SpansOf(states);
    ASSERT_EQ(split.size(), 2u);
    ExpectSpan(split[0], Range(0, 1, 0, 0, ImageAspectFlags::Depth), depthRead);
    ExpectSpan(split[1], Range(0, 1, 0, 0, ImageAspectFlags::Stencil), depthWrite);

    const SpanList stencil = SpansOf(states, Aspect(ImageAspectFlags::Stencil));
    ASSERT_EQ(stencil.size(), 1u);
    ExpectSpan(stencil[0], Range(0, 1, 0, 0, ImageAspectFlags::Stencil), depthWrite);

    states.Set(Aspect(ImageAspectFlags::Stencil), depthRead);
    EXPECT_TRUE(states.IsUniform());
}

TEST(ImageSubresourceStatesTest, PlanesSplitTheSameWayGoOutAsOneSpan)
{
    ImageSubresourceStates states(3, 1, ImageAspectFlags::DepthStencil, Sampled);
    states.Set(Mips(2, 2), Storage);

    const SpanList spans = SpansOf(states);
    ASSERT_EQ(spans.size(), 2u);
    ExpectSpan(spans[0], Range(0, 1, 0, 0, ImageAspectFlags::DepthStencil), Sampled);
    ExpectSpan(spans[1], Range(2, 2, 0, 0, ImageAspectFlags::DepthStencil), Storage);
}
