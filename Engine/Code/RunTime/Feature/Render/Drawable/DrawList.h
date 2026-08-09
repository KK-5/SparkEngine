#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIHandle.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

namespace Spark::RHI
{
    class PipelineState;
    class ShaderBindings;
}

namespace Spark::Render
{
    //! Until per-draw PSO variants land, every draw in a pass shares the pass PSO, so
    //! every list holds exactly one batch.
    inline constexpr uint16_t kSingleVariantId = 0;

    //! What separates one DrawList from another within a pass. Only the view today.
    struct DrawListKey
    {
        RHI::RHIHandle m_view = RHI::NullHandle;

        bool operator==(const DrawListKey& other) const { return m_view == other.m_view; }
    };

    //! A run of consecutive entries sharing one PSO. ExecuteIndirect cannot switch PSO
    //! within a call, so this split outlives the CPU submit path.
    struct DrawBatch
    {
        uint32_t                  m_begin     = 0;
        uint32_t                  m_end       = 0;
        uint16_t                  m_variantId = kSingleVariantId;
        const RHI::PipelineState* m_pso       = nullptr;
    };

    //! One view's worth of a pass's draws. m_entries holds DrawItem ENTITIES, not
    //! pointers — entt's dense storage swaps elements on removal, which would silently
    //! invalidate pointers. Ordered by m_variantId so every batch is a contiguous range.
    struct DrawList
    {
        DrawListKey                   m_key;
        RHI::Viewport                 m_viewport {};
        RHI::Scissor                  m_scissor {};
        const RHI::ShaderBindings*    m_viewBindings = nullptr;
        eastl::vector<RHI::RHIHandle> m_entries;
        eastl::vector<DrawBatch>      m_batches;
    };

    //! Lives on the pass entity. A derived cache of the PassTag membership record, so it
    //! can be thrown away and rebuilt at any time.
    struct PassDrawLists
    {
        eastl::vector<DrawList> m_lists;
    };

    //! Insert into the batch holding variantId, creating that batch if it is the first of
    //! its kind. O(batch count), not O(entries): every batch after the target one moves a
    //! single element to reopen the boundary.
    void DrawListInsert(DrawList& list, RHI::RHIHandle entry, uint16_t variantId);

    //! Remove the entry at index. Order within a batch is not preserved.
    void DrawListRemoveAt(DrawList& list, uint32_t index);

    //! Per-frame reconcile of every pass's lists against the keys it declares, plus a
    //! sweep of entries whose DrawItem is gone.
    //!
    //! Must run BEFORE DrawItemRouter: a list created this frame is filled by the full
    //! query (which cannot see this frame's not-yet-derived DrawItems), and the router
    //! then inserts those into every list including the new one — the two paths cover
    //! disjoint sets, so nothing is inserted twice.
    void BuildPassDrawLists();
}
