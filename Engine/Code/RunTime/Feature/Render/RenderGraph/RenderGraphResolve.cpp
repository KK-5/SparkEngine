#include "RenderGraphResolve.h"

#include <EASTL/algorithm.h>
#include <EASTL/functional.h>
#include <EASTL/heap.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

#include <Log/ILogSystem.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/Component/ScopeComponents.h>

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t kNoPass = ~0u;

        //! An attachment as the resolution works on it. A pass is its place in the declaration
        //! order here, and in everything below.
        struct Access
        {
            RHIHandle m_attachment {NullHandle};
            RHIHandle m_resource {NullHandle};
            uint32_t  m_order   = 0;    //!< its place among the frame's attachments
            uint32_t  m_pass    = 0;
            bool      m_writes  = false;
            uint32_t  m_version = 0;
        };

        //! m_from runs before m_to.
        struct Edge
        {
            uint32_t m_from = 0;
            uint32_t m_to   = 0;

            bool operator==(const Edge& other) const
            {
                return m_from == other.m_from && m_to == other.m_to;
            }
        };

        AttachmentId& GetAttachmentId(RHI::RHIContext& rhiContext, RHIHandle attachment)
        {
            auto* image = rhiContext.TryGet<ImagePassAttachment>(attachment);
            return image ? image->m_attachmentId : rhiContext.Get<BufferPassAttachment>(attachment).m_attachmentId;
        }

        //! Reads the attachments that are linked to a resource into `accesses`, one resource's
        //! together and in the order they were declared.
        void CollectAccesses(
            RHI::RHIContext& rhiContext, eastl::span<const Pass> passes, eastl::span<const RHI::RHIHandle> attachments,
            eastl::vector<Access>& accesses)
        {
            eastl::unordered_map<Pass, uint32_t> passIndex;
            for (uint32_t i = 0; i < static_cast<uint32_t>(passes.size()); ++i)
            {
                passIndex.emplace(passes[i], i);
            }

            accesses.reserve(attachments.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(attachments.size()); ++i)
            {
                Access access;
                access.m_attachment = attachments[i];
                access.m_resource   = rhiContext.Get<ScopeAttachment>(attachments[i]).m_resource;
                access.m_order      = i;
                if (access.m_resource == NullHandle)
                {
                    continue;
                }

                const auto* image  = rhiContext.TryGet<ImagePassAttachment>(attachments[i]);
                const auto* buffer = rhiContext.TryGet<BufferPassAttachment>(attachments[i]);
                const auto  found  = passIndex.find(image ? image->m_pass : buffer->m_pass);
                ASSERT(found != passIndex.end(),
                    "[RenderGraphResolve] An attachment's pass was not declared through CreatePass.");
                access.m_pass   = found->second;
                access.m_writes = CheckBitsAny(image ? image->m_access : buffer->m_access, RHI::AttachmentAccess::Write);
                accesses.push_back(access);
            }

            // m_order keeps a resource's attachments as declared: which write a read sees
            // depends on it.
            eastl::sort(accesses.begin(), accesses.end(), [](const Access& lhs, const Access& rhs)
            {
                return lhs.m_resource != rhs.m_resource ? lhs.m_resource < rhs.m_resource : lhs.m_order < rhs.m_order;
            });
        }

        //! Gives one resource's accesses their versions, and writes them to the attachments.
        //! `producers` comes back as the pass that left each version; nothing left version 0.
        void AssignVersions(
            RHI::RHIContext& rhiContext, eastl::span<Access> accesses, eastl::vector<uint32_t>& producers,
            eastl::vector<GraphError>& errors)
        {
            // A write leaves the next version, a read sees the latest so far.
            producers.clear();
            producers.push_back(kNoPass);
            for (Access& access : accesses)
            {
                if (access.m_writes)
                {
                    producers.push_back(access.m_pass);
                }
                access.m_version = static_cast<uint32_t>(producers.size()) - 1;
            }
            const uint32_t last = static_cast<uint32_t>(producers.size()) - 1;

            // A read with no write before it, of a resource that starts with nothing in it:
            // what it means is what the frame leaves there, written by passes declared later.
            if (rhiContext.Has<TransientTag>(accesses.front().m_resource))
            {
                for (Access& access : accesses)
                {
                    if (access.m_writes || access.m_version != 0)
                    {
                        continue;
                    }
                    // Its own pass's write comes after it within the pass, whatever the order
                    // of the passes.
                    if (last == 0 || producers[last] == access.m_pass)
                    {
                        errors.push_back(GraphError{ GraphErrorType::ReadsUndefined, access.m_attachment });
                        continue;
                    }
                    access.m_version = last;
                }
            }

            for (const Access& access : accesses)
            {
                GetAttachmentId(rhiContext, access.m_attachment).m_version = access.m_version;
            }
        }

        //! Adds the edges one resource's accesses ask for, given the pass that left each of its
        //! versions. An edge may be added more than once; none is from a pass to itself, which
        //! is how a pass reads in one Scope what it wrote in another.
        void AddEdges(eastl::span<const Access> accesses, const eastl::vector<uint32_t>& producers, eastl::vector<Edge>& edges)
        {
            const uint32_t last = static_cast<uint32_t>(producers.size()) - 1;
            const auto addEdge = [&](uint32_t from, uint32_t to)
            {
                if (from != kNoPass && to != kNoPass && from != to)
                {
                    edges.push_back(Edge{ from, to });
                }
            };

            for (const Access& access : accesses)
            {
                if (access.m_writes)
                {
                    // After the version it writes over, whether or not it reads it.
                    addEdge(producers[access.m_version - 1], access.m_pass);
                }
                else
                {
                    // After the version it reads is written, and before the next one is written
                    // over that.
                    addEdge(producers[access.m_version], access.m_pass);
                    if (access.m_version < last)
                    {
                        addEdge(access.m_pass, producers[access.m_version + 1]);
                    }
                }
            }
        }

        //! Gives every pass with an access its PassGlobalTimeline: Kahn's algorithm, taking of
        //! the passes that wait for nothing the one declared first. `edges` are sorted by
        //! m_from and each there once. Passes the edges leave in a cycle, and those waiting
        //! for them, get none and are added to `unordered`.
        void PlacePasses(
            PassContext& passContext, eastl::span<const Pass> passes, const eastl::vector<Access>& accesses,
            const eastl::vector<Edge>& edges, eastl::vector<Pass>& unordered)
        {
            const uint32_t passCount = static_cast<uint32_t>(passes.size());

            // waiting[p]: p has an access and no place yet.
            // waitsFor[p]: how many passes that run before p have no place yet.
            // firstEdge[p]: where p's edges start in `edges`; they end where p + 1's start.
            eastl::vector<uint8_t>  waiting(passCount, 0);
            eastl::vector<uint32_t> waitsFor(passCount, 0u);
            eastl::vector<uint32_t> firstEdge(passCount + 1, 0u);
            for (const Access& access : accesses)
            {
                waiting[access.m_pass] = 1;
            }
            // Count p's edges into the slot after p's; summing the slots from the left then
            // leaves in p's slot how many edges the passes before p have, which is where p's
            // start.
            for (const Edge& edge : edges)
            {
                ++waitsFor[edge.m_to];
                ++firstEdge[edge.m_from + 1];
            }
            for (uint32_t pass = 0; pass < passCount; ++pass)
            {
                firstEdge[pass + 1] += firstEdge[pass];
            }

            // A min-heap on the declaration index; filled in ascending order, it is one already.
            eastl::vector<uint32_t> ready;
            const eastl::greater<uint32_t> later;
            for (uint32_t pass = 0; pass < passCount; ++pass)
            {
                if (waiting[pass] != 0 && waitsFor[pass] == 0)
                {
                    ready.push_back(pass);
                }
            }

            uint32_t position = 0;
            while (!ready.empty())
            {
                eastl::pop_heap(ready.begin(), ready.end(), later);
                const uint32_t pass = ready.back();
                ready.pop_back();
                passContext.Add<PassGlobalTimeline>(passes[pass], PassGlobalTimeline{ position++ });
                waiting[pass] = 0;

                // The passes that waited for this one wait for one pass fewer.
                for (uint32_t i = firstEdge[pass]; i < firstEdge[pass + 1]; ++i)
                {
                    const uint32_t next = edges[i].m_to;
                    if (--waitsFor[next] == 0)
                    {
                        ready.push_back(next);
                        eastl::push_heap(ready.begin(), ready.end(), later);
                    }
                }
            }

            for (uint32_t pass = 0; pass < passCount; ++pass)
            {
                if (waiting[pass] != 0)
                {
                    unordered.push_back(passes[pass]);
                }
            }
        }
    }

    void ResolveGraph(
        RHI::RHIContext& rhiContext, PassContext& passContext, eastl::span<const RHI::RHIHandle> attachments,
        GraphResolution& out)
    {
        out.m_errors.clear();
        out.m_edges.clear();
        out.m_unordered.clear();

        const eastl::span<const Pass> passes = passContext.GetPassesInDeclOrder();

        eastl::vector<Access> accesses;
        CollectAccesses(rhiContext, passes, attachments, accesses);

        // A resource at a time: its versions, then the edges they ask for.
        eastl::vector<Edge>     edges;
        eastl::vector<uint32_t> producers;
        for (Access* begin = accesses.begin(); begin != accesses.end();)
        {
            Access* end = begin;
            while (end != accesses.end() && end->m_resource == begin->m_resource)
            {
                ++end;
            }

            AssignVersions(rhiContext, eastl::span<Access>(begin, end), producers, out.m_errors);
            AddEdges(eastl::span<const Access>(begin, end), producers, edges);
            begin = end;
        }

        eastl::sort(edges.begin(), edges.end(), [](const Edge& lhs, const Edge& rhs)
        {
            return lhs.m_from != rhs.m_from ? lhs.m_from < rhs.m_from : lhs.m_to < rhs.m_to;
        });
        edges.erase(eastl::unique(edges.begin(), edges.end()), edges.end());
        for (const Edge& edge : edges)
        {
            out.m_edges.push_back(GraphEdge{ passes[edge.m_from], passes[edge.m_to] });
        }

        PlacePasses(passContext, passes, accesses, edges, out.m_unordered);
    }
}
