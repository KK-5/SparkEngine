#pragma once

#include <cstdint>

#include <EASTL/fixed_vector.h>

#include <RHI/Command/DrawArguments.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/RHILimits.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>

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

    //! Marker for a composed Drawable. Carries no back-pointer to the world
    //! entity. Lifecycle cascades through DeadTag from referenced resource
    //! entities (VertexStreams buffers, IndexBufferRef, InstanceBindingRef)
    //! and from the InstanceSlotTable sentinel.
    struct DrawableTag {};

    //! Placed on a WORLD entity once DrawableComposer has produced its
    //! Drawable. Filters the find-or-create predicate so already-composed
    //! entities are skipped. A system that invalidates the Drawable's
    //! resources (mesh swap etc.) is responsible for removing this tag so
    //! the composer rebuilds next frame.
    struct WorldComposedTag {};

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

    //! All vertex streams the Drawable feeds into the IA. Slot is declared
    //! per entry; list order is not significant. Slots must form a contiguous
    //! 0..N-1 set (validated by VertexBufferView).
    struct VertexStreams
    {
        eastl::fixed_vector<VertexStreamSpec, RHI::Limits::Pipeline::StreamCountMax> m_streams;
    };

    //! Optional index buffer. Component presence ⟺ indexed draw.
    struct IndexBufferRef
    {
        RHI::RHIHandle  m_buffer = RHI::NullHandle;
        IndexBufferInfo m_info;
    };

    //! Reference to the global g_Instances ShaderBindings entity. Recorded
    //! per-Drawable (not cached in Processors) so cascade reap reacts
    //! uniformly when InstanceBindingSystem rebuilds the entity.
    struct InstanceBindingRef
    {
        RHI::RHIHandle m_binding = RHI::NullHandle;
    };

    //! Geometry-determined draw arguments (IndexCount / BaseVertex /
    //! StartIndex). StartInstanceLocation lives in InstanceSlotRef.
    struct DrawGeomArgs
    {
        RHI::DrawArguments m_args;
    };

    //! GPU instance count for this Drawable. Always 1 in the un-batched path;
    //! a future Batcher writes N for merged same-mesh draws.
    struct DrawInstancing
    {
        uint32_t m_instanceCount = 1;
    };

    // InstanceSlotRef (from Instance/InstanceSlot.h) is also a component on
    // the Drawable, copied once by DrawableComposer. See that header for the
    // table-indirection contract.
}
