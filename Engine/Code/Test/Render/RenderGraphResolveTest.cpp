#include <gtest/gtest.h>

#include <initializer_list>

#include <EASTL/sort.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/Component/ScopeComponents.h>
#include <RenderGraph/RenderGraphResolve.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    using Indices = eastl::vector<uint32_t>;

    //! An edge by the passes' indices.
    struct Edge
    {
        uint32_t m_from = 0;
        uint32_t m_to   = 0;

        bool operator==(const Edge& other) const
        {
            return m_from == other.m_from && m_to == other.m_to;
        }
    };
    using Edges = eastl::vector<Edge>;
}

//! A frame built by hand in contexts of its own: passes and resources are named by the order
//! they were made in, attachments by the order they were declared in.
class RenderGraphResolveTest : public ::testing::Test
{
protected:
    //! The resources in `transient` start with nothing in them; the others hold something
    //! at version 0, as an imported resource does.
    void Declare(uint32_t passCount, uint32_t resourceCount, std::initializer_list<uint32_t> transient = {})
    {
        for (uint32_t i = 0; i < passCount; ++i)
        {
            m_passes.push_back(m_passContext.CreatePass());
        }
        for (uint32_t i = 0; i < resourceCount; ++i)
        {
            m_resources.push_back(m_rhiContext.CreateEntity());
        }
        for (uint32_t resource : transient)
        {
            m_rhiContext.Add<TransientTag>(m_resources[resource]);
        }
    }

    void Read(uint32_t pass, uint32_t resource)
    {
        Attach(pass, resource, RHI::AttachmentAccess::Read);
    }

    void Write(uint32_t pass, uint32_t resource)
    {
        Attach(pass, resource, RHI::AttachmentAccess::Write);
    }

    void ReadWrite(uint32_t pass, uint32_t resource)
    {
        Attach(pass, resource, RHI::AttachmentAccess::ReadWrite);
    }

    void Resolve()
    {
        ResolveGraph(
            m_rhiContext, m_passContext,
            eastl::span<const RHI::RHIHandle>(m_attachments.data(), m_attachments.size()), m_graph);
    }

    Indices Versions()
    {
        Indices versions;
        for (RHI::RHIHandle attachment : m_attachments)
        {
            versions.push_back(m_rhiContext.Get<ImagePassAttachment>(attachment).m_attachmentId.m_version);
        }
        return versions;
    }

    //! The passes that got a place in the frame, by it.
    Indices Order()
    {
        eastl::vector<eastl::pair<uint32_t, uint32_t>> placed;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_passes.size()); ++i)
        {
            if (const auto* timeline = m_passContext.TryGet<PassGlobalTimeline>(m_passes[i]))
            {
                placed.push_back({ timeline->m_position, i });
            }
        }
        eastl::sort(placed.begin(), placed.end());

        Indices order;
        for (uint32_t i = 0; i < static_cast<uint32_t>(placed.size()); ++i)
        {
            EXPECT_EQ(placed[i].first, i);
            order.push_back(placed[i].second);
        }
        return order;
    }

    Edges GraphEdges() const
    {
        Edges edges;
        for (const GraphEdge& edge : m_graph.m_edges)
        {
            edges.push_back(Edge{ IndexOf(edge.m_from), IndexOf(edge.m_to) });
        }
        return edges;
    }

    Indices Unordered() const
    {
        Indices passes;
        for (Pass pass : m_graph.m_unordered)
        {
            passes.push_back(IndexOf(pass));
        }
        return passes;
    }

    //! The attachments at fault, by the order they were declared in.
    Indices ErrorAttachments() const
    {
        Indices attachments;
        for (const GraphError& error : m_graph.m_errors)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_attachments.size()); ++i)
            {
                if (m_attachments[i] == error.m_attachment)
                {
                    attachments.push_back(i);
                }
            }
        }
        return attachments;
    }

    GraphResolution m_graph;

private:
    void Attach(uint32_t pass, uint32_t resource, RHI::AttachmentAccess access)
    {
        ImagePassAttachment attachment;
        attachment.m_access = access;
        attachment.m_pass   = m_passes[pass];
        attachment.m_image  = m_resources[resource];

        const RHI::RHIHandle handle = m_rhiContext.CreateEntity();
        m_rhiContext.Add<ImagePassAttachment>(handle, attachment);
        m_rhiContext.Add<ScopeAttachment>(handle, ScopeAttachment{ RHI::NullHandle, m_resources[resource] });
        m_attachments.push_back(handle);
    }

    uint32_t IndexOf(Pass pass) const
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_passes.size()); ++i)
        {
            if (m_passes[i] == pass)
            {
                return i;
            }
        }
        ADD_FAILURE() << "A pass the test did not make.";
        return 0;
    }

    RHI::RHIContext               m_rhiContext;
    PassContext                   m_passContext;
    eastl::vector<Pass>           m_passes;
    eastl::vector<RHI::RHIHandle> m_resources;
    eastl::vector<RHI::RHIHandle> m_attachments;
};

TEST_F(RenderGraphResolveTest, ReadersSeeWhatTheWriterLeftAndRunAfterIt)
{
    Declare(3, 1);
    Write(0, 0);
    Read(1, 0);
    Read(2, 0);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 1, 1 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 }, { 0, 2 } }));
    EXPECT_EQ(Order(), (Indices{ 0, 1, 2 }));
    EXPECT_TRUE(m_graph.m_errors.empty());
    EXPECT_TRUE(m_graph.m_unordered.empty());
}

// SceneColor through Lights, IndirectDiffuse, Reflections and a reader.
TEST_F(RenderGraphResolveTest, EachWriteLeavesTheNextVersionOverTheOneBefore)
{
    Declare(4, 1);
    Write(0, 0);
    ReadWrite(1, 0);
    ReadWrite(2, 0);
    Read(3, 0);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 2, 3, 3 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 }, { 1, 2 }, { 2, 3 } }));
    EXPECT_EQ(Order(), (Indices{ 0, 1, 2, 3 }));
}

TEST_F(RenderGraphResolveTest, AWriteThatReadsNothingStillFollowsTheVersionBefore)
{
    Declare(2, 1);
    Write(0, 0);
    Write(1, 0);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 2 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 } }));
}

TEST_F(RenderGraphResolveTest, AReaderRunsBeforeTheNextVersionIsWritten)
{
    Declare(3, 1);
    Write(0, 0);
    Read(1, 0);
    ReadWrite(2, 0);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 1, 2 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 }, { 0, 2 }, { 1, 2 } }));
}

// An imported resource: version 0 has content and no pass that left it.
TEST_F(RenderGraphResolveTest, AReadOfVersionZeroWaitsForNothing)
{
    Declare(3, 1);
    Read(0, 0);
    Write(1, 0);
    Read(2, 0);
    Resolve();

    EXPECT_TRUE(m_graph.m_errors.empty());
    EXPECT_EQ(Versions(), (Indices{ 0, 1, 1 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 }, { 1, 2 } }));
}

// A mip chain: pass 1 reads resource 0, then each of its Scopes reads the mip above and writes
// its own, all of resource 1.
TEST_F(RenderGraphResolveTest, APassThatReadsWhatItWroteHasNoEdgeToItself)
{
    Declare(3, 2);
    Write(0, 0);
    Read(1, 0);
    Write(1, 1);
    Read(1, 1);
    Write(1, 1);
    Read(1, 1);
    Write(1, 1);
    Read(2, 1);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 1, 1, 1, 2, 2, 3, 3 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 }, { 1, 2 } }));
    EXPECT_EQ(Order(), (Indices{ 0, 1, 2 }));
}

// Five mips written through five views in one Scope.
TEST_F(RenderGraphResolveTest, SeveralWritesInOneScopeEachLeaveAVersion)
{
    Declare(2, 1);
    Write(0, 0);
    Write(0, 0);
    Write(0, 0);
    Read(1, 0);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 2, 3, 3 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 } }));
}

TEST_F(RenderGraphResolveTest, SeveralAccessesBetweenTwoPassesMakeOneEdge)
{
    Declare(2, 2);
    Write(0, 0);
    Write(0, 1);
    Read(1, 0);
    Read(1, 0);
    Read(1, 1);
    Resolve();

    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 } }));
}

TEST_F(RenderGraphResolveTest, PassesTheEdgesDoNotOrderRunAsDeclared)
{
    Declare(4, 2);
    Write(0, 0);
    Write(1, 1);
    Read(2, 1);
    Read(3, 0);
    Resolve();

    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 3 }, { 1, 2 } }));
    EXPECT_EQ(Order(), (Indices{ 0, 1, 2, 3 }));
}

// Pass 1 declared nothing; pass 2 only reads a resource nothing writes, as last frame's copy
// of a name is.
TEST_F(RenderGraphResolveTest, OnlyPassesWithAnAttachmentGetAPlace)
{
    Declare(3, 2);
    Write(0, 0);
    Read(2, 1);
    Resolve();

    EXPECT_EQ(Versions(), (Indices{ 1, 0 }));
    EXPECT_TRUE(GraphEdges().empty());
    EXPECT_EQ(Order(), (Indices{ 0, 2 }));
    EXPECT_TRUE(m_graph.m_unordered.empty());
}

// The producer is declared after its reader: registration order does not matter where it is
// unambiguous.
TEST_F(RenderGraphResolveTest, AReadOfATransientWithNoWriteBeforeItSeesTheLastWrite)
{
    Declare(3, 1, { 0 });
    Read(0, 0);
    Write(1, 0);
    ReadWrite(2, 0);
    Resolve();

    EXPECT_TRUE(m_graph.m_errors.empty());
    EXPECT_EQ(Versions(), (Indices{ 2, 1, 2 }));
    EXPECT_EQ(GraphEdges(), (Edges{ { 1, 2 }, { 2, 0 } }));
    EXPECT_EQ(Order(), (Indices{ 1, 2, 0 }));
}

TEST_F(RenderGraphResolveTest, AReadBetweenTheWritesOfATransientSeesTheOneBeforeIt)
{
    Declare(3, 1, { 0 });
    Write(0, 0);
    Read(1, 0);
    ReadWrite(2, 0);
    Resolve();

    EXPECT_TRUE(m_graph.m_errors.empty());
    EXPECT_EQ(Versions(), (Indices{ 1, 1, 2 }));
}

TEST_F(RenderGraphResolveTest, AReadOfATransientNothingWritesIsAnError)
{
    Declare(2, 2, { 0, 1 });
    Write(0, 0);
    Read(1, 1);
    Resolve();

    EXPECT_EQ(ErrorAttachments(), (Indices{ 1 }));
    EXPECT_EQ(m_graph.m_errors[0].m_type, GraphErrorType::ReadsUndefined);
}

// The write is in a later Scope of the reader's own pass, so it cannot come first.
TEST_F(RenderGraphResolveTest, AReadAheadOfItsOwnPassesFirstWriteIsAnError)
{
    Declare(2, 1, { 0 });
    Read(0, 0);
    Write(0, 0);
    Read(1, 0);
    Resolve();

    EXPECT_EQ(ErrorAttachments(), (Indices{ 0 }));
}

// Pass 0 reads what pass 1 leaves, and pass 1 reads what pass 0 leaves.
TEST_F(RenderGraphResolveTest, PassesThatNeedEachOtherGetNoPlace)
{
    Declare(3, 3, { 0, 1, 2 });
    Read(0, 0);
    Write(0, 1);
    Read(1, 1);
    Write(1, 0);
    Write(2, 2);
    Resolve();

    EXPECT_TRUE(m_graph.m_errors.empty());
    EXPECT_EQ(GraphEdges(), (Edges{ { 0, 1 }, { 1, 0 } }));
    EXPECT_EQ(Order(), (Indices{ 2 }));
    EXPECT_EQ(Unordered(), (Indices{ 0, 1 }));
}

TEST_F(RenderGraphResolveTest, AFrameWithoutAttachmentsResolvesToNothing)
{
    Declare(2, 0);
    Resolve();

    EXPECT_TRUE(GraphEdges().empty());
    EXPECT_TRUE(Order().empty());
    EXPECT_TRUE(m_graph.m_unordered.empty());
}
