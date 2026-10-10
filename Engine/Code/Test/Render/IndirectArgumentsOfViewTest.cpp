#include <gtest/gtest.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/Component/RHIComponents.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    //! An access of `buffer` that writes `view`'s indirect arguments at `byteOffset`.
    RHIHandle AddWriteOfView(RHIContext& context, RHIHandle buffer, RHIHandle view, uint64_t byteOffset)
    {
        BufferPassAttachment access;
        access.m_buffer = buffer;

        const RHIHandle attachment = context.CreateEntity();
        context.Add<BufferPassAttachment>(attachment, access);
        context.Add<IndirectArgumentsOfView>(attachment, IndirectArgumentsOfView{ view, byteOffset });
        return attachment;
    }
}

TEST(IndirectArgumentsOfViewTest, AViewReadsFromWhereItsOwnStart)
{
    RHIContext context;
    const RHIHandle buffer = context.CreateEntity();
    const RHIHandle viewA  = context.CreateEntity();
    const RHIHandle viewB  = context.CreateEntity();
    AddWriteOfView(context, buffer, viewA, 0);
    AddWriteOfView(context, buffer, viewB, 2560);

    uint64_t byteOffset = ~0ull;
    EXPECT_TRUE(TryGetIndirectArgumentsOfView(context, buffer, viewA, byteOffset));
    EXPECT_EQ(byteOffset, 0u);
    EXPECT_TRUE(TryGetIndirectArgumentsOfView(context, buffer, viewB, byteOffset));
    EXPECT_EQ(byteOffset, 2560u);
}

//! The argument buffer and the count buffer are marked for the same view at offsets of
//! their own.
TEST(IndirectArgumentsOfViewTest, EachBufferHasOffsetsOfItsOwn)
{
    RHIContext context;
    const RHIHandle arguments = context.CreateEntity();
    const RHIHandle count     = context.CreateEntity();
    const RHIHandle view      = context.CreateEntity();
    AddWriteOfView(context, arguments, view, 2560);
    AddWriteOfView(context, count, view, 4);

    uint64_t byteOffset = 0;
    EXPECT_TRUE(TryGetIndirectArgumentsOfView(context, arguments, view, byteOffset));
    EXPECT_EQ(byteOffset, 2560u);
    EXPECT_TRUE(TryGetIndirectArgumentsOfView(context, count, view, byteOffset));
    EXPECT_EQ(byteOffset, 4u);
}

//! What lowering tells apart: a buffer read from its start under any view, and one that
//! holds a part per view but none for the view asked about.
TEST(IndirectArgumentsOfViewTest, AViewWithNoPartIsNotTakenForAnUnmarkedBuffer)
{
    RHIContext context;
    const RHIHandle marked   = context.CreateEntity();
    const RHIHandle unmarked = context.CreateEntity();
    const RHIHandle viewA    = context.CreateEntity();
    const RHIHandle viewB    = context.CreateEntity();
    AddWriteOfView(context, marked, viewA, 0);

    BufferPassAttachment access;
    access.m_buffer = unmarked;
    context.Add<BufferPassAttachment>(context.CreateEntity(), access);

    uint64_t byteOffset = 0;
    EXPECT_TRUE(HoldsIndirectArgumentsPerView(context, marked));
    EXPECT_FALSE(TryGetIndirectArgumentsOfView(context, marked, viewB, byteOffset));

    EXPECT_FALSE(HoldsIndirectArgumentsPerView(context, unmarked));
    EXPECT_FALSE(TryGetIndirectArgumentsOfView(context, unmarked, viewA, byteOffset));
}
