#pragma once

#include <EASTL/functional.h>
#include <EASTL/type_traits.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>
#include <Pass/Pass.h>

namespace Spark::RHI
{
    class PipelineState;
    class ShaderBindings;
}

namespace Spark::Render
{
    struct ExecuteWork;
    class RenderGraphExecuter;

    //! An ordered stretch of a pass: within it items do not depend on each other and each
    //! resource is used one way. A per-frame entity in RHIContext, where all its attachments
    //! and items live.
    struct Scope
    {
        Pass     m_pass {NullPass};
        uint32_t m_index {0};   //!< order within m_pass
    };

    //! On every attachment, naming the Scope it belongs to. Links point from attachment to
    //! Scope; a Scope holds no attachment list. Items and selections get a component of their
    //! own: nothing ever walks attachments and items together.
    struct ScopeAttachment
    {
        RHI::RHIHandle m_scope {RHI::NullHandle};
    };

    //! On a Scope whose access to some resource follows one on another queue: per source queue,
    //! the latest producer Scope it must wait for, and the fence value that works out to.
    //! CompileScopeBarriers fills m_producer, CompileScopeSync the values in stream order.
    //! A zero value means no wait on that queue — none needed, or an earlier wait covers it.
    static_assert(RHI::HardwareQueueClassCount == 3, "ScopeWait's initializer lists one producer per queue.");

    struct ScopeWait
    {
        RHI::RHIHandle m_producer[RHI::HardwareQueueClassCount] { RHI::NullHandle, RHI::NullHandle, RHI::NullHandle };
        uint64_t       m_value[RHI::HardwareQueueClassCount] {};
    };

    //! On a Scope another queue waits for. The value is its queue's next fence value, assigned
    //! by CompileScopeSync in stream order so each queue signals monotonically.
    struct ScopeSignal
    {
        uint64_t m_value = 0;
    };

    //! The part of a Scope's submit state its pass decides: the PSO and the bindings bound once
    //! for the whole Scope (the pass's own space2 and those it declared via .Binds). Copied from
    //! the pass by lowering. Viewport and space1 come from the view handles in the Scope's
    //! submit range.
    struct ScopeState
    {
        const RHI::PipelineState*  m_pso = nullptr;
        const RHI::ShaderBindings* m_bindings[RHI::Limits::Pipeline::ShaderInputGroupCountMax] {};
        uint8_t                    m_bindingCount = 0;
    };

    //! The Scope's stretch of the executer's submit list: per ready view, the view's handle and
    //! then the items submitted under it; a viewless Scope has items only. A view gets its
    //! handle even when no item follows — nothing assumes the CPU knows how many there are.
    struct ScopeSubmitRange
    {
        uint32_t m_begin = 0;
        uint32_t m_end   = 0;
    };

    //! On a Scope whose work is opaque: the executer hands its submit range to this hook once
    //! per view segment (empty ones included), or once if the range is empty, instead of
    //! submitting the items itself. Points into the pass's PassFunctions.
    struct ScopeExecute
    {
        const eastl::function<void(ExecuteWork&, RenderGraphExecuter&)>* m_execute = nullptr;
    };

    //! On a Scope that starts a new CommandList. Nothing adds it yet.
    struct WorkStartTag {};

    // Lowering sorts these storages, which entt refuses on an in_place_delete storage holding
    // tombstones — a trivially movable type keeps the default swap-and-pop policy.
    static_assert(eastl::is_trivially_copyable_v<Scope>);
    static_assert(eastl::is_trivially_copyable_v<ScopeAttachment>);
}
