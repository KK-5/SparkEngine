#pragma once

#include <cstdint>

#include <EASTL/fixed_vector.h>

#include <RHI/Command/DrawArguments.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>

#include <Instance/InstanceSlot.h>

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
        RHI::RHIHandle m_buffer     = RHI::NullHandle;
        uint32_t       m_inputSlot  = 0;
        uint32_t       m_byteOffset = 0;
        uint32_t       m_byteCount  = 0;
        uint32_t       m_byteStride = 0;
    };

    //! Marker for a composed Drawable. Lifecycle cascades through DeadTag from
    //! referenced resource entities (Drawable.m_streams buffers, m_indexBuffer,
    //! m_instanceBinding) and from the InstanceSlotTable sentinel.
    struct DrawableTag {};

    //! Placed on a WORLD entity once DrawableComposer has produced its
    //! Drawable. Filters the find-or-create predicate so already-composed
    //! entities are skipped. A system that invalidates the Drawable's
    //! resources (mesh swap etc.) is responsible for removing this tag so
    //! the composer rebuilds next frame.
    struct WorldComposedTag {};

    //! Per-object draw recipe assembled by DrawableComposer. All fields are
    //! always populated together (composer is the single writer), so they
    //! live in one struct rather than separate components — every consumer
    //! reads them in lockstep and the split bought nothing but TryGet noise.
    //!
    //!  - m_streams           : IA streams, slots declared per entry; the layout
    //!                          contract (contiguous 0..N-1) is validated by
    //!                          VertexBufferView.
    //!  - m_indexBuffer       : NullHandle ⟺ non-indexed draw.
    //!  - m_instanceBinding   : reference to the global g_Instances
    //!                          ShaderBindings entity. Recorded per-Drawable
    //!                          so cascade reap reacts uniformly when the
    //!                          bindings entity is rebuilt.
    //!  - m_slotRef           : stable id copy. The per-frame slot lookup
    //!                          (InstanceSlotTable.m_slots[m_id]) happens at
    //!                          CompileDrawRequests time.
    //!  - m_drawArgs          : geometry-determined draw args (IndexCount /
    //!                          BaseVertex / StartIndex). StartInstanceLocation
    //!                          is resolved from m_slotRef each frame.
    //!  - m_instanceCount     : GPU instance count. Always 1 in the un-batched
    //!                          path; a future Batcher writes N for merged
    //!                          same-mesh draws.
    struct Drawable
    {
        eastl::fixed_vector<VertexStreamSpec, RHI::Limits::Pipeline::StreamCountMax> m_streams;
        RHI::RHIHandle     m_indexBuffer     = RHI::NullHandle;
        IndexBufferInfo    m_indexInfo;
        RHI::RHIHandle     m_instanceBinding = RHI::NullHandle;
        InstanceSlotRef    m_slotRef;
        RHI::DrawArguments m_drawArgs;
        uint32_t           m_instanceCount   = 1;
    };
}
