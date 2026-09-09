#pragma once

#include <EASTL/utility.h>

#include <entt/entt.hpp>

namespace Spark
{
    /// @brief A batch of entities and components that is not wired to any system.
    ///
    /// It holds the same thing a context holds, but it is not one: nothing observes it, it never
    /// enters the ExecuteContext stack, and it can only ever be the source of a Merge — never the
    /// target. That makes the direction of a merge, and the ban on self-merging, compile-time facts.
    ///
    /// Producers are the deserializer, Extract, undo bookkeeping and prefab sources.
    ///
    /// It does not preserve identifiers verbatim: Merge creates target entities with CreateEntity(hint)
    /// and the registry silently renumbers on collision.
    template<typename EntityType>
    class StagingContext
    {
    public:
        using Entity = EntityType;

        StagingContext() = default;
        ~StagingContext() noexcept
        {
            Clear();
        }

        StagingContext(StagingContext&&) noexcept = default;
        StagingContext& operator=(StagingContext&&) noexcept = default;

        StagingContext(const StagingContext&) = delete;
        StagingContext& operator=(const StagingContext&) = delete;

        void Clear()
        {
            m_registry.clear();
        }

        Entity CreateEntity()
        {
            return m_registry.create();
        }

        /// @brief Create an entity at the given identifier. When the slot is already taken the
        /// registry silently picks another one, so always use the returned value.
        Entity CreateEntity(Entity hint)
        {
            return m_registry.create(hint);
        }

        bool Valid(Entity entity) const noexcept
        {
            return m_registry.valid(entity);
        }

        /// @brief The live entity occupying the same identifier slot as hint, regardless of version.
        Entity EntityAt(Entity hint) const noexcept
        {
            using Traits = entt::entt_traits<EntityType>;
            const Entity candidate = Traits::construct(entt::to_entity(hint), m_registry.current(hint));
            return m_registry.valid(candidate) ? candidate : Entity{entt::null};
        }

        template<typename T, typename... Args>
        decltype(auto) Add(Entity entity, Args&&... args)
        {
            return m_registry.template emplace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename... T>
        decltype(auto) Get(Entity entity) const
        {
            return eastl::as_const(m_registry).template get<T...>(entity);
        }

        template<typename... T>
        decltype(auto) Get(Entity entity)
        {
            return m_registry.template get<T...>(entity);
        }

        template<typename T>
        bool Has(Entity entity) const
        {
            return m_registry.template any_of<T>(entity);
        }

        template<typename... T>
        bool HasAll(Entity entity) const
        {
            return m_registry.template all_of<T...>(entity);
        }

        template<typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) const
        {
            return eastl::as_const(m_registry).template view<Component...>(excludes);
        }

        template<typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{})
        {
            return m_registry.template view<Component...>(excludes);
        }

        /// @brief Direct access to a component storage. Creates it when missing.
        template<typename T>
        decltype(auto) GetStorage()
        {
            return m_registry.template storage<T>();
        }

        /// @brief Returns nullptr when the storage does not exist — the const overload only looks up.
        template<typename T>
        decltype(auto) GetStorage() const
        {
            return eastl::as_const(m_registry).template storage<T>();
        }

    private:
        entt::basic_registry<EntityType> m_registry{};
    };
}
