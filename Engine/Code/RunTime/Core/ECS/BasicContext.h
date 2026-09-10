#pragma once

#include "ContextStorage.h"

namespace Spark
{
    /// @brief A live context: entities and components that systems operate on.
    ///
    /// The generic template dispatches nothing. Specialise it for an entity type that needs events
    /// on top — see BasicContext<Entity> in WorldContext.h.
    template<typename EntityType>
    class BasicContext : public ContextStorage<EntityType>
    {
    public:
        using Entity = EntityType;

        BasicContext() = default;
        ~BasicContext() noexcept
        {
            this->Clear();
        }

        BasicContext(const BasicContext&) = delete;
        BasicContext& operator=(const BasicContext&) = delete;

        // Reserved extension points. BasicContext itself does not dispatch bus events.
        template<typename Component>
        void RegisterEventOnEntityRemove()
        {
        }

        template<typename... Components>
        void RegisterEventsOnEntityRemove()
        {
        }
    };
}
