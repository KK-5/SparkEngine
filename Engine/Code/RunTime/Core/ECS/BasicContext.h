#pragma once

#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/utility.h>

#include <entt/entt.hpp>

#include "CoreComponents/Name.h"

namespace Spark
{
    template<typename... Type>
    inline constexpr entt::exclude_t<Type...> Exclude{};

    template<typename... Type>
    inline constexpr entt::get_t<Type...> Include{};

    template<typename EntityType>
    class BasicContext final
    {
    public:
        using Entity = EntityType;

        BasicContext() = default;
        ~BasicContext() noexcept
        {
            Clear();
        }

        BasicContext(const BasicContext&) = delete;
        BasicContext& operator=(const BasicContext&) = delete;

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
        decltype(auto) Repalce(Entity entity, Args&&... args)
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

        // Reserved extension points. BasicContext itself does not dispatch bus events.
        template<typename Component>
        void RegisterEventOnEntityRemove()
        {
        }

        template<typename... Components>
        void RegisterEventsOnEntityRemove()
        {
        }

    private:
        entt::basic_registry<EntityType> m_registry{};
    };
}
