#pragma once

#include <cstdint>

#include <EASTL/fixed_vector.h>
#include <EASTL/variant.h>

#include <RHI/Command/DrawArguments.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>

#include <Instance/InstanceSlot.h>

namespace Spark::RHI
{
    class ShaderBindings;
}

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

    //! Marker for a composed Drawable. Lifecycle cascades through DeadTag from
    //! referenced resource entities (Drawable.m_streams buffers, m_index.m_indexBuffer,
    //! and the m_instanceData strategy's dependencies — shared bindings + ID
    //! stream, or the per-draw bindings) and from the InstanceSlotTable sentinel.
    struct DrawableTag {};

    //! Placed on a WORLD entity once DrawableComposer has produced its
    //! Drawable. Filters the find-or-create predicate so already-composed
    //! entities are skipped. A system that invalidates the Drawable's
    //! resources (mesh swap etc.) is responsible for removing this tag so
    //! the composer rebuilds next frame.
    struct WorldComposedTag {};

    //! Per-object data provisioning — STRATEGY 1 (indexed). Per-object data
    //! (model matrix, …) lives in a shared GPU buffer; this draw occupies one
    //! slot. Resolving the draw needs three coupled parts:
    //!  - m_sharedBindings : the global g_Instances ShaderBindings entity, which
    //!                       also hosts the InstanceSlotTable. Bound as a SRG
    //!                       (space4) AND consulted for the slot → startInstance
    //!                       mapping. Recorded per-Drawable so cascade reap
    //!                       reacts uniformly when the bindings entity rebuilds.
    //!  - m_slotRef        : stable id copy. The per-frame slot lookup
    //!                       (InstanceSlotTable.m_slots[m_id]) → StartInstanceLocation
    //!                       happens in BindPassDrawItems each frame.
    //!  - m_idStream       : the identity ID buffer ([0..Cap-1]) bound as a
    //!                       per-instance vertex stream — the only way to feed
    //!                       StartInstanceLocation into the VS (SV_InstanceID
    //!                       always starts at 0). Present only on this path.
    struct SlotInstanceBinding
    {
        RHI::RHIHandle   m_sharedBindings = RHI::NullHandle;
        InstanceSlotRef  m_slotRef;
        VertexStreamSpec m_idStream;
    };

    //! Per-object data provisioning — STRATEGY 2 (direct). This draw carries its
    //! own per-draw ShaderBindings (e.g. a small CBV holding the model matrix).
    //! No shared buffer, no slot table, no ID stream; StartInstanceLocation = 0.
    //! The right choice for a one-off / simple object that would rather upload
    //! its data directly than route through the global instance buffer.
    struct DirectInstanceBinding
    {
        RHI::RHIHandle m_bindings = RHI::NullHandle;
    };

    //! Per-object data provisioning — STRATEGY 0 (none). The draw has no per-object
    //! data at all: no shared buffer, no per-draw SRG, no ID stream, StartInstance = 0.
    //! Used by procedural draws (e.g. a full-screen skybox triangle) that still flow
    //! through the single DeriveDrawItems translation but carry no geometry instancing.
    //! Listed first so a default-constructed Drawable provisions nothing.
    struct NoInstanceBinding {};

    //! Per-object draw recipe assembled by a Drawable producer. The geometry +
    //! draw args + instance count are always populated together (single writer),
    //! so they live in one struct rather than separate components — every
    //! consumer reads them in lockstep and the split bought nothing but TryGet
    //! noise.
    //!
    //!  - m_streams        : real geometry IA streams only (slot 0…), slots
    //!                       declared per entry; the layout contract (contiguous
    //!                       0..N-1) is validated by VertexBufferView. The
    //!                       instance-ID stream is NOT here — it belongs to the
    //!                       SlotInstanceBinding strategy.
    //!  - m_index          : index buffer + range; m_index.m_indexBuffer is
    //!                       NullHandle ⟺ non-indexed draw.
    //!  - m_drawArgs       : geometry-determined draw args (IndexCount /
    //!                       BaseVertex / StartIndex). StartInstanceLocation is
    //!                       resolved from m_instanceData each frame.
    //!  - m_instanceCount  : GPU instance count. Always 1 in the un-batched
    //!                       path; a future Batcher writes N for merged
    //!                       same-mesh draws.
    //!  - m_instanceData   : how this draw obtains its per-object data + GPU
    //!                       instance index. A union-like variant (no heap), so
    //!                       Drawable stays a complete, self-contained recipe
    //!                       without baking in the indexed-buffer assumption.
    struct Drawable
    {
        eastl::fixed_vector<VertexStreamSpec, RHI::Limits::Pipeline::StreamCountMax> m_streams;
        IndexStreamSpec    m_index;
        RHI::DrawArguments m_drawArgs;
        uint32_t           m_instanceCount = 1;

        eastl::variant<NoInstanceBinding, SlotInstanceBinding, DirectInstanceBinding> m_instanceData;
    };

    //! Upper bound on DrawItems one Drawable derives (one per consuming pass). Single
    //! digit today; headroom for shadow cascades. Inline, so it must never overflow.
    inline constexpr size_t MaxPassesPerDrawable = 8;

    //! Producer-side reverse reference to the DrawItems a Drawable derived, for cascade
    //! teardown (and later culling propagation). The submit path never touches it —
    //! DrawItems are self-contained. Present ⟺ composed Drawable (added empty at create).
    struct DerivedDrawItems
    {
        eastl::fixed_vector<RHI::RHIHandle, MaxPassesPerDrawable> m_items;
    };

    //! Idempotency marker for the generic DrawItem-derivation step: present once a
    //! Drawable has been routed through the passes and its DrawItems built. The
    //! derivation view excludes it so each Drawable derives exactly once, regardless of
    //! who produced it (world composer, procedural pass, …). Distinct from
    //! DerivedDrawItems, which is added empty at create — that can't double as the
    //! filter. Dies with the Drawable entity.
    struct DrawItemsDerivedTag {};

    //! Startinstance resolution key baked onto a DrawItem (indexed producers only) so
    //! the submit path never reaches back to a producer. m_slotRef is stable for the
    //! renderable's life; submit reads InstanceSlotTable.m_slots[m_slotRef] each frame.
    struct DrawItemInstanceSlot
    {
        InstanceSlotRef m_slotRef;
    };

    //! Per-object SRG (space4), a PER-DRAW binding resolved from the Drawable's
    //! provisioning at compose: shared g_Instances for indexed, the draw's own CBV for
    //! direct, absent for procedural. Carried here (not a shared tag) because direct's
    //! CBV is per-draw, not a singleton. The per-frame bind flow prepends it.
    struct DrawItemObjectBinding
    {
        const RHI::ShaderBindings* m_objShaderBindings = nullptr;
    };
}
