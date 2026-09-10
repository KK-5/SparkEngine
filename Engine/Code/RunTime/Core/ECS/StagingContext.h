#pragma once

#include "ContextStorage.h"

namespace Spark
{
    /// @brief A batch of entities and components that is not wired to any system.
    ///
    /// It holds what a context holds, but it is not one: nothing observes it, it never enters the
    /// ExecuteContext stack, and it can only ever be the source of a Merge — never the target. That
    /// makes the direction of a merge, and the ban on self-merging, compile-time facts.
    ///
    /// Producers are the deserializer, Extract, undo bookkeeping and prefab sources.
    ///
    /// It does not preserve identifiers verbatim: Merge creates target entities with
    /// CreateEntity(hint) and the registry silently renumbers on collision.
    ///
    /// A sibling of BasicContext rather than a derived class — inheriting would let it bind to
    /// Merge's target parameter and lose the direction guarantee.
    template<typename EntityType>
    class StagingContext : public ContextStorage<EntityType>
    {
    public:
        using Entity = EntityType;

        StagingContext() = default;
        ~StagingContext() noexcept = default;

        StagingContext(StagingContext&&) noexcept = default;
        StagingContext& operator=(StagingContext&&) noexcept = default;

        StagingContext(const StagingContext&) = delete;
        StagingContext& operator=(const StagingContext&) = delete;
    };
}
