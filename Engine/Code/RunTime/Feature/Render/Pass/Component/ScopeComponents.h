#pragma once

#include <EASTL/type_traits.h>

#include <RHI/Context/RHIContext.h>
#include <Pass/Pass.h>

namespace Spark::Render
{
    //! An ordered stretch of a pass: within it items do not depend on each other and each
    //! resource is used one way. A per-frame entity in RHIContext, where all its members live.
    struct Scope
    {
        Pass     m_pass {NullPass};
        uint32_t m_index {0};   //!< order within m_pass
    };

    //! On every attachment, single item and selection that belongs to a Scope. Links point from
    //! member to Scope; a Scope holds no member list.
    struct ScopeMember
    {
        RHI::RHIHandle m_scope {RHI::NullHandle};
    };

    // Lowering sorts both storages, which entt refuses on an in_place_delete storage holding
    // tombstones — a trivially movable type keeps the default swap-and-pop policy.
    static_assert(eastl::is_trivially_copyable_v<Scope>);
    static_assert(eastl::is_trivially_copyable_v<ScopeMember>);
}
