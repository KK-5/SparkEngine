#pragma once

#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/algorithm.h>

#include <entt/entt.hpp>

#include "BasicWorldContext.h"
#include "Entity.h"
#include "Reflection/RTTI.h"
#include "Bus/EntityEventBus.h"
#include "Bus/ComponentEventBus.h"
#include "CoreComponents/Name.h"
#include "ComponentTraits.h"

namespace Spark
{
    template<typename... Type>
    inline constexpr entt::exclude_t<Type...> Exclude{};

    template<typename... Type>
    inline constexpr entt::get_t<Type...> Include{};

    namespace Internal
    {
        template<typename... Cs>
        struct AnyComponentTraitsRemoveEvent;

        template<typename Head, typename... Tail>
        struct AnyComponentTraitsRemoveEvent<Head, Tail...>
        {
            static constexpr bool value =
                ((ComponentTraits<Head>::componentEvents & ComponentEventMask::Remove) != ComponentEventMask::None)
                || AnyComponentTraitsRemoveEvent<Tail...>::value;
        };

        template<>
        struct AnyComponentTraitsRemoveEvent<>
        {
            static constexpr bool value = false;
        };
    }

    template<>
    class BasicWorldContext<Entity> final
    {
    public:
        BasicWorldContext() = default;
        ~BasicWorldContext() noexcept
        {
            Clear();
        }

        BasicWorldContext(const BasicWorldContext&) = delete;
        BasicWorldContext& operator=(const BasicWorldContext&) = delete;

        void Clear()
        {
            m_registry.clear();
            m_entityRemoveEvents.clear();
        }
        
        // Entity operation
        Entity CreateEntity()
        {
            Entity entity = m_registry.create();

            if (entity != NullEntity)
            {
                EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntityCreate, entity);
            }
            return entity;
        }

        Entity CreateEntity(eastl::string_view name)
        {
            Entity entity = m_registry.create();
            if (entity != NullEntity)
            {
                Add<Name>(entity, eastl::string(name));
                EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntityCreate, entity);
            }

            return entity;
        }

        void DestoryEntity(Entity entity)
        {
            EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntityDestory, entity);
            DispatchComponentDestoryEventsForEntityDestroy(entity);
            m_registry.destroy(entity);
        }

        template <typename It>
        void CreateEntity(It first, It last)
        {
            m_registry.create(first, last);

            for(It cur = first; cur != last; ++cur)
            {
                EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntityCreate, *cur);
            }
        }

        template <typename It>
        void DestoryEntity(It first, It last)
        {
            for(It cur = first; cur != last; ++cur)
            {
                EntityEventBus::Broadcast(&EntityEventBus::Events::OnEntityDestory, *cur);
                DispatchComponentDestoryEventsForEntityDestroy(*cur);
            }

            m_registry.destroy(first, last);
        }

        bool Valid(Entity entity) const noexcept
        {
            return m_registry.valid(entity);
        }

        //////////////////////////////////////////
        // Add Component
        template<typename T, typename... Args, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & ComponentEventMask::Create) == ComponentEventMask::None, int> = 0>
        decltype(auto) Add(Entity entity, Args&&... args)
        {
            return m_registry.emplace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename... Args, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & ComponentEventMask::Create) != ComponentEventMask::None, int> = 0>
        decltype(auto) Add(Entity entity, Args&&... args)
        {
            decltype(auto) result = m_registry.emplace<T>(entity, eastl::forward<Args>(args)...);
            ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentConstruct, *this, entity);
            return result;
        }

        template<typename T, typename It, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & ComponentEventMask::Create) == ComponentEventMask::None, int> = 0>
        void Add(It first, It last, const T& value)
        {
            m_registry.insert(first, last, value);
        }

        template<typename T, typename It, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & ComponentEventMask::Create) != ComponentEventMask::None, int> = 0>
        void Add(It first, It last, const T& value)
        {
            m_registry.insert(first, last, value);
            eastl::for_each(first, last, [&](auto entity)
            {
                ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentConstruct, *this, entity);
            });
        }
        //////////////////////////////////////////

        //////////////////////////////////////////
        // Update Component
        template<typename T, typename... Args, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & (ComponentEventMask::Create | ComponentEventMask::WillUpdate | ComponentEventMask::Updated)) == ComponentEventMask::None, int> = 0>
        decltype(auto) AddOrRepalce(Entity entity, Args&&... args)
        {
            return m_registry.emplace_or_replace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename... Args, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & (ComponentEventMask::Create | ComponentEventMask::WillUpdate | ComponentEventMask::Updated)) != ComponentEventMask::None, int> = 0>
        decltype(auto) AddOrRepalce(Entity entity, Args&&... args)
        {
            const bool had = m_registry.template any_of<T>(entity);

            if constexpr ((ComponentTraits<T>::componentEvents & ComponentEventMask::WillUpdate) != ComponentEventMask::None)
            {
                if (had)
                {
                    ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentWillUpdate, *this, entity);
                }
            }

            decltype(auto) result = m_registry.emplace_or_replace<T>(entity, eastl::forward<Args>(args)...);

            if constexpr ((ComponentTraits<T>::componentEvents & ComponentEventMask::Create) != ComponentEventMask::None)
            {
                if (!had)
                {
                    ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentConstruct, *this, entity);
                }
            }

            if constexpr ((ComponentTraits<T>::componentEvents & ComponentEventMask::Updated) != ComponentEventMask::None)
            {
                if (had)
                {
                    ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentUpdated, *this, entity);
                }
            }

            return result;
        }

        template<typename T, typename... Args, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & (ComponentEventMask::WillUpdate | ComponentEventMask::Updated)) == ComponentEventMask::None, int> = 0>
        decltype(auto) Repalce(Entity entity, Args&&... args)
        {
            return m_registry.replace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename... Args, eastl::enable_if_t<(ComponentTraits<T>::componentEvents & (ComponentEventMask::WillUpdate | ComponentEventMask::Updated)) != ComponentEventMask::None, int> = 0>
        decltype(auto) Repalce(Entity entity, Args&&... args)
        {
            if constexpr ((ComponentTraits<T>::componentEvents & ComponentEventMask::WillUpdate) != ComponentEventMask::None)
            {
                ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentWillUpdate, *this, entity);
            }

            decltype(auto) result = m_registry.replace<T>(entity, eastl::forward<Args>(args)...);

            if constexpr ((ComponentTraits<T>::componentEvents & ComponentEventMask::Updated) != ComponentEventMask::None)
            {
                ComponentEventBus::Event(GetTypeId<T>(), &ComponentEventBus::Events::OnComponentUpdated, *this, entity);
            }

            return result;
        }
        //////////////////////////////////////////
        
        template<typename... T>
        decltype(auto) Get(Entity entity) const
        {
            return eastl::as_const(m_registry).get<T...>(entity);
        }

        template<typename... T>
        decltype(auto) Get(Entity entity)
        {
            return m_registry.get<T...>(entity);
        }

        template<typename... T>
        decltype(auto) TryGet(Entity entity)
        {
            return m_registry.try_get<T...>(entity);
        }

        //////////////////////////////////////////
        // Remove Component
        template<typename Type, typename... Other, eastl::enable_if_t<Internal::AnyComponentTraitsRemoveEvent<Type, Other...>::value, int> = 0>
        decltype(auto) Remove(Entity entity)
        {
            DispatchComponentRemoveBusIfForEntity<Type, Other...>(entity);
            return m_registry.remove<Type, Other...>(entity);
        }

        template<typename Type, typename... Other, eastl::enable_if_t<!Internal::AnyComponentTraitsRemoveEvent<Type, Other...>::value, int> = 0>
        decltype(auto) Remove(Entity entity)
        {
            return m_registry.remove<Type, Other...>(entity);
        }

        template<typename Type, typename... Other, typename It, eastl::enable_if_t<Internal::AnyComponentTraitsRemoveEvent<Type, Other...>::value, int> = 0>
        decltype(auto) Remove(It first, It last)
        {
            for (It cur = first; cur != last; ++cur)
            {
                DispatchComponentRemoveBusIfForEntity<Type, Other...>(*cur);
            }
            return m_registry.remove<Type, Other..., It>(first, last);
        }

        template<typename Type, typename... Other, typename It, eastl::enable_if_t<!Internal::AnyComponentTraitsRemoveEvent<Type, Other...>::value, int> = 0>
        decltype(auto) Remove(It first, It last)
        {
            return m_registry.remove<Type, Other..., It>(first, last);
        }
        //////////////////////////////////////////

        template<typename T>
        bool Has(Entity entity) const
        {
            return m_registry.any_of<T>(entity);
        }
        
        template<typename... T>
        bool HasAny(Entity entity) const
        {
            return m_registry.any_of<T...>(entity);
        }

        template<typename... T>
        bool HasAll(Entity entity) const
        {
            return m_registry.all_of<T...>(entity);
        }

        template<typename Owned, typename... Component, typename... Exclude>
        decltype(auto) CreateGroup(entt::get_t<Component...> gets = entt::get_t{}, 
            entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) {
            return m_registry.group<Owned>(gets, excludes);
        }

        template<typename Owned, typename... Component, typename... Exclude>
        decltype(auto) CreateGroup(entt::get_t<Component...> gets = entt::get_t{}, 
            entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) const {
            return eastl::as_const(m_registry).group<Owned>(gets, excludes);
        }

        template<typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) {
            return m_registry.view<Component...>(excludes);
        }

        template<typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) const {
            return eastl::as_const(m_registry).view<Component...>(excludes);
        }


        /// @brief Explicitly opt in component destroy events when an entity is destroyed.
        /// This does NOT replace/remove the existing Remove() path behavior.
        template<typename Component>
        void RegisterEventOnEntityRemove()
        {
            static_assert((ComponentTraits<Component>::componentEvents & ComponentEventMask::Remove) != ComponentEventMask::None,
                "RegisterEventOnEntityRemove requires ComponentTraits<Component>::componentEvents to include ComponentEventMask::Remove");

            m_entityRemoveEvents.emplace(GetTypeId<Component>());
        }

        template<typename... Components>
        void RegisterEventsOnEntityRemove()
        {
            (RegisterEventOnEntityRemove<Components>(), ...);
        }

    private:
        void DispatchComponentDestoryEventsForEntityDestroy(Entity entity)
        {
            if (m_entityRemoveEvents.empty())
            {
                return;
            }

            for (auto&& [storageTypeId, cpool] : m_registry.storage())
            {
                if (!cpool.contains(entity))
                {
                    continue;
                }

                if (!m_entityRemoveEvents.contains(storageTypeId))
                {
                    continue;
                }

                ComponentEventBus::Event(storageTypeId, &ComponentEventBus::Events::OnComponentDestory, *this, entity);
            }
        }

        template<typename C>
        void DispatchComponentRemoveBusIf([[maybe_unused]] Entity entity)
        {
            if constexpr ((ComponentTraits<C>::componentEvents & ComponentEventMask::Remove) != ComponentEventMask::None)
            {
                if (m_registry.template any_of<C>(entity))
                {
                    ComponentEventBus::Event(GetTypeId<C>(), &ComponentEventBus::Events::OnComponentDestory, *this, entity);
                }
            }
        }

        template<typename... Cs>
        void DispatchComponentRemoveBusIfForEntity(Entity entity)
        {
            (DispatchComponentRemoveBusIf<Cs>(entity), ...);
        }

        entt::basic_registry<Entity> m_registry{};
        eastl::unordered_set<TypeId> m_entityRemoveEvents{};
    };

    using WorldContext = BasicWorldContext<Entity>;
}