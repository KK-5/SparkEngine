#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <entt/entt.hpp>

#include <Reflection/RTTI.h>

#include "CoreComponents/Name.h"

namespace Spark
{
    template<typename... Type>
    inline constexpr entt::exclude_t<Type...> Exclude{};

    template<typename... Type>
    inline constexpr entt::get_t<Type...> Include{};

    /// @brief Entities and components, and nothing that watches them.
    ///
    /// Both a live context and a staging one are this; they differ in who observes them and in what
    /// they are allowed to be the source or target of. Operations that need entity-component data
    /// and nothing else take this type, so they serve either.
    ///
    /// Writing through it dispatches nothing. A context that has events layers them on top.
    template<typename EntityType>
    class ContextStorage
    {
    public:
        using Entity = EntityType;

        /// @brief A component storage with its type erased. The only entt type this class
        /// hands out, and the reason GetStorages() can exist without exposing the registry.
        using ComponentStorage = typename entt::basic_registry<EntityType>::common_type;

        struct StorageEntry
        {
            TypeId                  type;
            const ComponentStorage* storage;
        };

        ContextStorage(const ContextStorage&) = delete;
        ContextStorage& operator=(const ContextStorage&) = delete;

        void Clear()
        {
            m_registry.clear();
        }

        template<typename Type, typename... Other>
        void Clear()
        {
            m_registry.template clear<Type, Other...>();
        }

        // Entity operation
        Entity CreateEntity()
        {
            return m_registry.create();
        }

        Entity CreateEntity(eastl::string_view name)
        {
            Entity entity = m_registry.create();
            if (entity != entt::null)
            {
                Add<Name>(entity, eastl::string(name));
            }
            return entity;
        }

        /// @brief Create an entity at the given identifier. When the slot is already taken the
        /// registry silently picks another one, so always use the returned value.
        Entity CreateEntity(Entity hint)
        {
            return m_registry.create(hint);
        }

        template<typename It>
        void CreateEntity(It first, It last)
        {
            m_registry.create(first, last);
        }

        void DestoryEntity(Entity entity)
        {
            m_registry.destroy(entity);
        }

        template<typename It>
        void DestoryEntity(It first, It last)
        {
            m_registry.destroy(first, last);
        }

        bool Valid(Entity entity) const noexcept
        {
            return m_registry.valid(entity);
        }

        /// @brief The live entity occupying the same identifier slot as hint, regardless of version.
        /// Valid() answers identity (slot + version), this answers occupancy (slot only) — which is
        /// what CreateEntity(hint) collides on.
        Entity EntityAt(Entity hint) const noexcept
        {
            using Traits = entt::entt_traits<EntityType>;
            const Entity candidate = Traits::construct(entt::to_entity(hint), m_registry.current(hint));
            return m_registry.valid(candidate) ? candidate : Entity{entt::null};
        }

        // Add Component
        template<typename T, typename... Args>
        decltype(auto) Add(Entity entity, Args&&... args)
        {
            return m_registry.template emplace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename It>
        void Add(It first, It last, const T& value)
        {
            m_registry.insert(first, last, value);
        }

        // Update Component
        template<typename T, typename... Args>
        decltype(auto) AddOrReplace(Entity entity, Args&&... args)
        {
            return m_registry.template emplace_or_replace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        decltype(auto) Replace(Entity entity, Args&&... args)
        {
            return m_registry.template replace<T>(entity, eastl::forward<Args>(args)...);
        }

        // Get Component
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

        template<typename... T>
        decltype(auto) TryGet(Entity entity) const
        {
            return eastl::as_const(m_registry).template try_get<T...>(entity);
        }

        template<typename... T>
        decltype(auto) TryGet(Entity entity)
        {
            return m_registry.template try_get<T...>(entity);
        }

        // Remove Component
        template<typename Type, typename... Other>
        decltype(auto) Remove(Entity entity)
        {
            return m_registry.template remove<Type, Other...>(entity);
        }

        template<typename Type, typename... Other, typename It>
        decltype(auto) Remove(It first, It last)
        {
            return m_registry.template remove<Type, Other..., It>(first, last);
        }

        // Query
        template<typename T>
        bool Has(Entity entity) const
        {
            return m_registry.template any_of<T>(entity);
        }

        template<typename... T>
        bool HasAny(Entity entity) const
        {
            return m_registry.template any_of<T...>(entity);
        }

        template<typename... T>
        bool HasAll(Entity entity) const
        {
            return m_registry.template all_of<T...>(entity);
        }

        // Group/View
        template<typename Owned, typename... Component, typename... Exclude>
        decltype(auto) CreateGroup(entt::get_t<Component...> gets = entt::get_t{},
            entt::exclude_t<Exclude...> excludes = entt::exclude_t{})
        {
            return m_registry.template group<Owned>(gets, excludes);
        }

        template<typename Owned, typename... Component, typename... Exclude>
        decltype(auto) CreateGroup(entt::get_t<Component...> gets = entt::get_t{},
            entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) const
        {
            return eastl::as_const(m_registry).template group<Owned>(gets, excludes);
        }

        template<typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{})
        {
            return m_registry.template view<Component...>(excludes);
        }

        template<typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) const
        {
            return eastl::as_const(m_registry).template view<Component...>(excludes);
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

        /// @brief Reorder T's storage in place: iterating it, or a view driven by it, then visits
        /// entities in ascending compare order. compare takes two entities or two const T&.
        ///
        /// The order holds only until T is next added or removed. Not allowed on a storage owned
        /// by a group, or on an in_place_delete storage that holds tombstones.
        template<typename T, typename Compare>
        void Sort(Compare compare)
        {
            m_registry.template sort<T>(eastl::move(compare));
        }

        /// @brief Every component storage, type-erased, for a reflection-driven walk --
        /// today only the scene writer. Business code that wants one component should name
        /// it: GetStorage<T>() is that path, and being explicit is the point.
        ///
        /// A snapshot rather than a callback or a view: the caller decides the order (the
        /// registry's own is the order storages happened to be created in, which is no
        /// basis for a file), and nothing it does during the walk can invalidate what it
        /// already holds. The entity storage is not in here: an entity the scene cares about is
        /// found through the component that says so, not by enumerating the registry.
        eastl::vector<StorageEntry> GetStorages() const
        {
            eastl::vector<StorageEntry> result;
            for (auto&& [id, storage] : eastl::as_const(m_registry).storage())
            {
                if (id != entt::type_hash<EntityType>::value())
                {
                    result.push_back({id, &storage});
                }
            }
            return result;
        }

        // Public and non-virtual on purpose. Nobody deletes a context through this type, so the
        // usual "protected or virtual" rule buys nothing here — and protected would make every
        // inherited member unreflectable, since entt::meta needs the class publicly destructible.
        ~ContextStorage() = default;

    protected:
        ContextStorage() = default;

        ContextStorage(ContextStorage&&) noexcept = default;
        ContextStorage& operator=(ContextStorage&&) noexcept = default;

        entt::basic_registry<EntityType> m_registry{};
    };
}
