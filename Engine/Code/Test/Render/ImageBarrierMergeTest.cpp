#include <gtest/gtest.h>

#include <RenderGraph/ImageBarrierMerge.h>

using namespace Spark;
using namespace Spark::RHI;
using namespace Spark::Render;

namespace
{
    constexpr HardwareQueueClass Queue = HardwareQueueClass::Graphics;
    constexpr AttachmentStage    Stage = AttachmentStage::ComputeShader;

    const AccessFlags StorageReadWrite = AccessFlags::ShaderStorageRead | AccessFlags::ShaderStorageWrite;

    const ResourceState Idle {};
    const ResourceState Sampled { AccessFlags::ShaderSampledRead, Queue, Stage };
    const ResourceState Storage { AccessFlags::ShaderStorageWrite, Queue, Stage };

    //! What a Scope on Queue asks of an image: nothing yet.
    ImageSubresourceStates Requests(uint16_t mipLevels, uint16_t arraySize, ImageAspectFlags aspects)
    {
        return ImageSubresourceStates(
            mipLevels, arraySize, aspects, ResourceState{ AccessFlags::None, Queue, AttachmentStage::Uninitialized });
    }

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

    ImageTransitionList Transitions(const ImageSubresourceStates& current, const ImageSubresourceStates& requests)
    {
        ImageTransitionList transitions;
        CollectImageTransitions(current, requests, transitions);
        return transitions;
    }

    //! A Scope's barriers applied: the tracker moves to what was asked.
    void Apply(ImageSubresourceStates& current, const ImageTransitionList& transitions)
    {
        for (const ImageTransition& transition : transitions)
        {
            current.Set(transition.m_range, transition.m_dst);
        }
    }

    void ExpectTransition(
        const ImageTransition& transition, const ImageSubresourceRange& range,
        const ResourceState& src, const ResourceState& dst)
    {
        EXPECT_TRUE(transition.m_range == range)
            << "mips [" << transition.m_range.m_mipSliceMin << ", " << transition.m_range.m_mipSliceMax
            << "] slices [" << transition.m_range.m_arraySliceMin << ", " << transition.m_range.m_arraySliceMax
            << "] aspects " << static_cast<uint32_t>(transition.m_range.m_aspectFlags);
        EXPECT_TRUE(transition.m_src == src);
        EXPECT_TRUE(transition.m_dst == dst);
    }
}

TEST(ImageBarrierMergeTest, AWholeImageAccessIsOneTransition)
{
    ImageSubresourceStates current(4, 1, ImageAspectFlags::Color);
    ImageSubresourceStates requests = Requests(4, 1, ImageAspectFlags::Color);

    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::ShaderSampledRead, Stage);
    EXPECT_TRUE(requests.IsUniform());

    const ImageTransitionList transitions = Transitions(current, requests);
    ASSERT_EQ(transitions.size(), 1u);
    ExpectTransition(transitions[0], Range(0, 3, 0, 0, ImageAspectFlags::Color), Idle, Sampled);
}

TEST(ImageBarrierMergeTest, WholeImageReadsMerge)
{
    ImageSubresourceStates requests = Requests(4, 1, ImageAspectFlags::Color);

    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::ShaderSampledRead, AttachmentStage::VertexShader);
    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::DepthStencilRead, AttachmentStage::FragmentShader);

    ASSERT_TRUE(requests.IsUniform());
    const ResourceState merged = requests.GetUniform();
    EXPECT_EQ(merged.m_access, AccessFlags::ShaderSampledRead | AccessFlags::DepthStencilRead);
    EXPECT_EQ(merged.m_stage, AttachmentStage::VertexShader | AttachmentStage::FragmentShader);
    EXPECT_EQ(merged.m_queue, Queue);
}

TEST(ImageBarrierMergeTest, StorageReadAndWriteMerge)
{
    ImageSubresourceStates requests = Requests(4, 1, ImageAspectFlags::Color);

    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::ShaderStorageRead, Stage);
    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::ShaderStorageWrite, Stage);
    EXPECT_EQ(requests.GetUniform().m_access, StorageReadWrite);
}

TEST(ImageBarrierMergeTest, ReadingOneMipAndWritingTheNextStayApart)
{
    ImageSubresourceStates current(4, 1, ImageAspectFlags::Color);
    ImageSubresourceStates requests = Requests(4, 1, ImageAspectFlags::Color);

    AccumulateImageAccess(requests, Mips(0, 0), AccessFlags::ShaderSampledRead, Stage);
    AccumulateImageAccess(requests, Mips(1, 1), AccessFlags::ShaderStorageWrite, Stage);

    // The two mips nothing asked for take no barrier.
    const ImageTransitionList transitions = Transitions(current, requests);
    ASSERT_EQ(transitions.size(), 2u);
    ExpectTransition(transitions[0], Range(0, 0, 0, 0, ImageAspectFlags::Color), Idle, Sampled);
    ExpectTransition(transitions[1], Range(1, 1, 0, 0, ImageAspectFlags::Color), Idle, Storage);
}

TEST(ImageBarrierMergeTest, AMipChainThenAReadOfAllOfIt)
{
    // Each Scope reads the mip above and writes its own; afterwards one view reads the chain.
    const uint16_t mipLevels = 4;
    ImageSubresourceStates current(mipLevels, 1, ImageAspectFlags::Color);

    {
        ImageSubresourceStates requests = Requests(mipLevels, 1, ImageAspectFlags::Color);
        AccumulateImageAccess(requests, Mips(0, 0), AccessFlags::ShaderStorageWrite, Stage);
        Apply(current, Transitions(current, requests));
    }
    for (uint16_t mip = 1; mip < mipLevels; ++mip)
    {
        ImageSubresourceStates requests = Requests(mipLevels, 1, ImageAspectFlags::Color);
        AccumulateImageAccess(requests, Mips(mip - 1, mip - 1), AccessFlags::ShaderSampledRead, Stage);
        AccumulateImageAccess(requests, Mips(mip, mip), AccessFlags::ShaderStorageWrite, Stage);

        const ImageTransitionList transitions = Transitions(current, requests);
        ASSERT_EQ(transitions.size(), 2u);
        ExpectTransition(transitions[0], Range(mip - 1, mip - 1, 0, 0, ImageAspectFlags::Color), Storage, Sampled);
        ExpectTransition(transitions[1], Range(mip, mip, 0, 0, ImageAspectFlags::Color), Idle, Storage);
        Apply(current, transitions);
    }
    EXPECT_FALSE(current.IsUniform());

    ImageSubresourceStates requests = Requests(mipLevels, 1, ImageAspectFlags::Color);
    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::ShaderSampledRead, Stage);

    // The last mip was left written, the others read.
    const ImageTransitionList transitions = Transitions(current, requests);
    ASSERT_EQ(transitions.size(), 2u);
    ExpectTransition(transitions[0], Range(0, 2, 0, 0, ImageAspectFlags::Color), Sampled, Sampled);
    ExpectTransition(transitions[1], Range(3, 3, 0, 0, ImageAspectFlags::Color), Storage, Sampled);

    Apply(current, transitions);
    EXPECT_TRUE(current.IsUniform());
    EXPECT_TRUE(current.GetUniform() == Sampled);
}

TEST(ImageBarrierMergeTest, ACubeSplitsByMipNotBySubresource)
{
    // The faces' last mip is written, the mips above it read: two rectangles, not twelve runs.
    ImageSubresourceStates current(3, 6, ImageAspectFlags::Color);
    current.Set(Mips(0, 1), Sampled);
    current.Set(Mips(2, 2), Storage);

    ImageSubresourceStates requests = Requests(3, 6, ImageAspectFlags::Color);
    AccumulateImageAccess(requests, ImageSubresourceRange(), AccessFlags::TransferRead, AttachmentStage::Copy);

    const ResourceState copied { AccessFlags::TransferRead, Queue, AttachmentStage::Copy };
    const ImageTransitionList transitions = Transitions(current, requests);
    ASSERT_EQ(transitions.size(), 2u);
    ExpectTransition(transitions[0], Range(0, 1, 0, 5, ImageAspectFlags::Color), Sampled, copied);
    ExpectTransition(transitions[1], Range(2, 2, 0, 5, ImageAspectFlags::Color), Storage, copied);
}

TEST(ImageBarrierMergeTest, DepthAndStencilAreAskedForSeparately)
{
    ImageSubresourceStates current(1, 1, ImageAspectFlags::DepthStencil);
    ImageSubresourceStates requests = Requests(1, 1, ImageAspectFlags::DepthStencil);

    AccumulateImageAccess(requests, Aspect(ImageAspectFlags::Depth), AccessFlags::ShaderSampledRead, Stage);
    AccumulateImageAccess(requests, Aspect(ImageAspectFlags::Stencil), AccessFlags::DepthStencilWrite, Stage);

    const ResourceState written { AccessFlags::DepthStencilWrite, Queue, Stage };
    const ImageTransitionList transitions = Transitions(current, requests);
    ASSERT_EQ(transitions.size(), 2u);
    ExpectTransition(transitions[0], Range(0, 0, 0, 0, ImageAspectFlags::Depth), Idle, Sampled);
    ExpectTransition(transitions[1], Range(0, 0, 0, 0, ImageAspectFlags::Stencil), Idle, written);
}

TEST(AccessConflictTest, ReadsDoNotConflict)
{
    EXPECT_FALSE(HasAccessConflict(AccessFlags::ShaderSampledRead, AccessFlags::ShaderSampledRead));
    EXPECT_FALSE(HasAccessConflict(AccessFlags::ShaderSampledRead, AccessFlags::DepthStencilRead));
}

TEST(AccessConflictTest, AWriteConflictsWithAnyOtherAccess)
{
    EXPECT_TRUE(HasAccessConflict(AccessFlags::ShaderSampledRead, AccessFlags::ColorAttachmentWrite));
    EXPECT_TRUE(HasAccessConflict(AccessFlags::ColorAttachmentWrite, AccessFlags::ColorAttachmentWrite));
    EXPECT_TRUE(HasAccessConflict(AccessFlags::DepthStencilRead, AccessFlags::DepthStencilWrite));
    EXPECT_TRUE(HasAccessConflict(AccessFlags::TransferWrite, AccessFlags::ShaderStorageWrite));
}

TEST(AccessConflictTest, StorageReadAndWriteAreTheException)
{
    EXPECT_FALSE(HasAccessConflict(AccessFlags::ShaderStorageRead, AccessFlags::ShaderStorageWrite));
    EXPECT_FALSE(HasAccessConflict(StorageReadWrite, StorageReadWrite));

    // Not once anything else joins them, nor a write with no read beside it.
    EXPECT_TRUE(HasAccessConflict(StorageReadWrite, AccessFlags::ShaderSampledRead));
    EXPECT_TRUE(HasAccessConflict(AccessFlags::ShaderStorageWrite, AccessFlags::ShaderStorageWrite));
}

TEST(ImageBarrierMergeTest, AnImageNothingAsksForTakesNoTransition)
{
    ImageSubresourceStates current(4, 1, ImageAspectFlags::Color);
    const ImageSubresourceStates requests = Requests(4, 1, ImageAspectFlags::Color);

    EXPECT_TRUE(Transitions(current, requests).empty());
}
