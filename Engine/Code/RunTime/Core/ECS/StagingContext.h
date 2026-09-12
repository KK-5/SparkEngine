#pragma once

#include <Log/ILogSystem.h>

#include "ComponentRuntime.h"
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

        using ContextStorage<EntityType>::Add;

        /// @brief A component described at runtime. The value carries its own type, and that
        /// type carries the only thing that cannot be worked out here: how to build its storage.
        ///
        /// The value is copied in and nothing is dispatched, as with every write to a staging
        /// context -- Merge announces the batch later.
        bool Add(EntityType entity, const MetaAny& value)
        {
            if (!value)
            {
                LOG_ERROR("[StagingContext] An empty value names no component type.");
                return false;
            }

            auto* storage = RuntimeComponentStorage<EntityType>(value.type(), *this);
            if (storage == nullptr)
            {
                LOG_ERROR("[StagingContext] {} takes no runtime data in this context.",
                    value.type().info().name());
                return false;
            }

            if (!this->Valid(entity))
            {
                LOG_ERROR("[StagingContext] Entity {} does not exist.", static_cast<uint32_t>(entity));
                return false;
            }

            // entt leaves a repeated push undefined.
            if (storage->contains(entity))
            {
                LOG_ERROR("[StagingContext] Entity {} already carries {}.",
                    static_cast<uint32_t>(entity), value.type().info().name());
                return false;
            }

            storage->push(entity, value.base().data());
            return true;
        }
    };
}
