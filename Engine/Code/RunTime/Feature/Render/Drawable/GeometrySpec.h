#pragma once

#include <cstdint>

#include <EASTL/fixed_vector.h>
#include <EASTL/variant.h>

#include <RHI/Command/DrawArguments.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>

#include <Binding/Instance/InstanceBinding.h>

namespace Spark::Render
{
    //! Range descriptor for a vertex stream's slice of its buffer.
    struct VertexBufferInfo
    {
        uint32_t m_byteOffset = 0;
        uint32_t m_byteCount  = 0;
        uint32_t m_byteStride = 0;
    };

    //! Range descriptor for an index buffer's slice.
    struct IndexBufferInfo
    {
        uint32_t         m_byteOffset = 0;
        uint32_t         m_byteCount  = 0;
        RHI::IndexFormat m_format     = RHI::IndexFormat::UINT32;
    };

    //! One vertex stream for a specific input slot. PSO InputStreamLayout
    //! decides per-vertex / per-instance interpretation; this only carries
    //! the buffer reference and range.
    struct VertexStreamSpec
    {
        RHI::RHIHandle   m_buffer     = RHI::NullHandle;
        uint32_t         m_inputSlot  = 0;
        VertexBufferInfo m_vertexBufferInfo;
    };

    struct IndexStreamSpec
    {
        RHI::RHIHandle  m_indexBuffer = RHI::NullHandle;
        IndexBufferInfo m_indexInfo;
    };

    //! Placed on a WORLD entity once MeshGeometryComposer has produced its
    //! GeometrySpec. Filters the find-or-create predicate so already-composed
    //! entities are skipped. A system that invalidates the spec's resources
    //! (mesh swap etc.) is responsible for removing this tag so the composer
    //! rebuilds next frame.
    struct WorldComposedTag {};

    //! Per-object data provisioning — STRATEGY 1 (indexed). Per-object data
    //! (model matrix, …) lives in the shared g_Instances buffer, bound at space4 by
    //! every pass that declares .Binds<InstanceBindingTag>(); this draw occupies one
    //! slot. Resolving the draw needs two coupled parts:
    //!  - m_slotRef        : a copy of the renderable's slot reference. m_id is the
    //!                       GPU index, baked into the DrawItem's
    //!                       StartInstanceLocation once at derive; IsValid() is the
    //!                       cascade-reap signal for when that index stops being ours.
    //!  - m_idStream       : the identity ID buffer ([0..Cap-1]) bound as a
    //!                       per-instance vertex stream — the only way to feed
    //!                       StartInstanceLocation into the VS (SV_InstanceID
    //!                       always starts at 0). Present only on this path.
    struct SlotInstanceBinding
    {
        InstanceSlotRef  m_slotRef;
        VertexStreamSpec m_idStream;
    };

    //! Per-object data provisioning — STRATEGY 0 (none). The draw has no per-object
    //! data at all: no shared buffer, no per-draw SRG, no ID stream, StartInstance = 0.
    //! Used by procedural draws (e.g. a full-screen skybox triangle) that still flow
    //! through the single DrawItemRouter translation but carry no geometry instancing.
    //! Listed first so a default-constructed GeometrySpec provisions nothing.
    struct NoInstanceBinding {};

    //! Per-object geometry specification: buffers referenced by ENTITY HANDLE plus byte
    //! ranges, draw args, and instance provisioning. All populated together (single
    //! writer), so they live in one struct rather than separate components — every
    //! consumer reads them in lockstep and the split bought nothing but TryGet noise.
    //!
    //! GeometrySpec and RHI::DrawItem are the unresolved and resolved forms of one object,
    //! on one entity. The spec can be written before its resources exist and holds the
    //! handles cascade reap follows; DrawItemRouter turns it into the DrawItem once every
    //! referenced buffer materializes, so `DrawItem` present ⟺ already derived.
    //!
    //! Lifecycle cascades through DeadTag from the referenced resource entities
    //! (m_streams buffers, m_index.m_indexBuffer, the m_instanceData strategy's ID stream)
    //! and from its instance slot going stale.
    //!
    //!  - m_streams        : real geometry IA streams only (slot 0…), slots
    //!                       declared per entry; the layout contract (contiguous
    //!                       0..N-1) is validated by VertexBufferView. The
    //!                       instance-ID stream is NOT here — it belongs to the
    //!                       SlotInstanceBinding strategy.
    //!  - m_index          : index buffer + range; m_index.m_indexBuffer is
    //!                       NullHandle ⟺ non-indexed draw.
    //!  - m_drawArgs       : geometry-determined draw args (IndexCount /
    //!                       BaseVertex / StartIndex). StartInstanceLocation comes
    //!                       from m_instanceData, baked once at derive.
    //!  - m_instanceCount  : GPU instance count. Always 1 in the un-batched
    //!                       path; a future Batcher writes N for merged
    //!                       same-mesh draws.
    //!  - m_instanceData   : how this draw obtains its per-object data + GPU
    //!                       instance index. A union-like variant (no heap), so
    //!                       GeometrySpec stays a complete, self-contained recipe
    //!                       without baking in the indexed-buffer assumption.
    struct GeometrySpec
    {
        eastl::fixed_vector<VertexStreamSpec, RHI::Limits::Pipeline::StreamCountMax> m_streams;
        IndexStreamSpec    m_index;
        RHI::DrawArguments m_drawArgs;
        uint32_t           m_instanceCount = 1;

        eastl::variant<NoInstanceBinding, SlotInstanceBinding> m_instanceData;
    };
}
