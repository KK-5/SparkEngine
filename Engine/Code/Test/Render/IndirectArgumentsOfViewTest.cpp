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
    EXPECT_TRUE(TryGetIndirectArgumentsStart(context, buffer, viewA, byteOffset));
    EXPECT_EQ(byteOffset, 0u);
    EXPECT_TRUE(TryGetIndirectArgumentsStart(context, buffer, viewB, byteOffset));
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
    EXPECT_TRUE(TryGetIndirectArgumentsStart(context, arguments, view, byteOffset));
    EXPECT_EQ(byteOffset, 2560u);
    EXPECT_TRUE(TryGetIndirectArgumentsStart(context, count, view, byteOffset));
    EXPECT_EQ(byteOffset, 4u);
}

//! A buffer no writer sets a start of, as the CPU writes its records, is read from its own
//! start under any view.
TEST(IndirectArgumentsOfViewTest, ABufferNoWriterSetsAStartOfStartsAtZero)
{
    RHIContext context;
    const RHIHandle unmarked = context.CreateEntity();
    const RHIHandle marked   = context.CreateEntity();
    const RHIHandle viewA    = context.CreateEntity();
    const RHIHandle viewB    = context.CreateEntity();
    AddWriteOfView(context, marked, viewA, 2560);

    BufferPassAttachment access;
    access.m_buffer = unmarked;
    context.Add<BufferPassAttachment>(context.CreateEntity(), access);

    uint64_t byteOffset = ~0ull;
    EXPECT_TRUE(TryGetIndirectArgumentsStart(context, unmarked, viewA, byteOffset));
    EXPECT_EQ(byteOffset, 0u);
    byteOffset = ~0ull;
    EXPECT_TRUE(TryGetIndirectArgumentsStart(context, unmarked, viewB, byteOffset));
    EXPECT_EQ(byteOffset, 0u);
}

//! A buffer whose writers set the start of other views has none for a view they leave out.
TEST(IndirectArgumentsOfViewTest, AViewTheWritersLeaveOutHasNoStart)
{
    RHIContext context;
    const RHIHandle buffer = context.CreateEntity();
    const RHIHandle viewA  = context.CreateEntity();
    const RHIHandle viewB  = context.CreateEntity();
    AddWriteOfView(context, buffer, viewA, 0);

    uint64_t byteOffset = 0;
    EXPECT_FALSE(TryGetIndirectArgumentsStart(context, buffer, viewB, byteOffset));
}
