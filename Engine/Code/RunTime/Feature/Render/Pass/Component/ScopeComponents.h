#pragma once

#include <EASTL/type_traits.h>

#include <RHI/Context/RHIContext.h>
#include <Pass/Pass.h>

namespace Spark::Render
{
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

    // Lowering sorts these storages, which entt refuses on an in_place_delete storage holding
    // tombstones — a trivially movable type keeps the default swap-and-pop policy.
    static_assert(eastl::is_trivially_copyable_v<Scope>);
    static_assert(eastl::is_trivially_copyable_v<ScopeAttachment>);
}
