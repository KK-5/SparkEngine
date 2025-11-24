#pragma once

#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unique_ptr.h>

#include <entt/entt.hpp>

#include "Entity.h"
#include "NameComponent.h"
#include "Reflection/RTTI.h"
#include "Bus/EntityEventBus.h"
#include "Bus/ComponentEventBus.h"

namespace Spark
{
    template<typename... Type>
    inline constexpr entt::exclude_t<Type...> Exclude{};

    template<typename... Type>
    inline constexpr entt::get_t<Type...> Include{};

    class WorldContext final
    {
    public:
        WorldContext() = default;
        ~WorldContext() noexcept
        {
            Clear();
        }

        WorldContext(const WorldContext&) = delete;
        WorldContext& operator=(const WorldContext&) = delete;

        void Clear()
        {
            m_registry.clear();
            m_forwarders.clear();
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
            }

            m_registry.destroy(first, last);
        }

        bool Valid(Entity entity) const noexcept
        {
            return m_registry.valid(entity);
        }

        // Component operation
        template<typename T>
        decltype(auto) Add(Entity entity, const T& component)
        {
            return m_registry.emplace<T>(entity, component);
        }

        template<typename T, typename... Args>
        decltype(auto) Add(Entity entity, Args... args)
        {
            return m_registry.emplace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename It>
        decltype(auto) Add(It first, It last, const T& component)
        {
            return m_registry.insert(first, last, component);
        }

        template<typename T, typename... Args>
        decltype(auto) AddOrRepalce(Entity entity, Args... args)
        {
            return m_registry.emplace_or_replace<T>(entity, eastl::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        decltype(auto) Repalce(Entity entity, Args... args)
        {
            return m_registry.replace<T>(entity, eastl::forward<Args>(args)...);
        }
        
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
        decltype(auto) TryGet(Entity entity) const
        {
            return m_registry.try_get<T...>(entity);
        }

        template<typename Type, typename... Other>
        decltype(auto) Remove(Entity entity)
        {
            return m_registry.remove<Type, Other...>(entity);
        }

        template<typename Type, typename... Other, typename It>
        decltype(auto) Remove(It first, It last)
        {
            return m_registry.remove<Type, Other..., It>(first, last);
        }

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

        /// @brief Setup component events listener.
        /// Only AddOrRepalce or Replace method can trigger update evnets.
        /// @tparam Component 
        template<typename Component>
        void SetupComponentEvents() 
        {
            // 绑定所有forwarder生命周期与WorldContext一致
            if (m_forwarders.contains(GetTypeId<Component>()))
            {
                return;
            }
            ComponentEventForwarder<Component>* forwarder = new ComponentEventForwarder<Component>(*this);
            m_forwarders.emplace(GetTypeId<Component>(), forwarder);

            m_registry.on_construct<Component>().connect<&ComponentEventForwarder<Component>::ForwardConstruct>(*forwarder);
            m_registry.on_update<Component>().connect<&ComponentEventForwarder<Component>::ForwardUpdate>(*forwarder);
            m_registry.on_destroy<Component>().connect<&ComponentEventForwarder<Component>::ForwardDestory>(*forwarder);
        }

        template<typename... Components>
        void SetupComponentsEvents()
        {
            (SetupComponentEvents<Components>(), ...);
        }

    private:
        struct Forwarder
        {
            virtual ~Forwarder() = default;

            virtual void ForwardConstruct(entt::registry& registry, entt::entity entity) = 0;

            virtual void ForwardUpdate(entt::registry& registry, entt::entity entity) = 0;

            virtual void ForwardDestory(entt::registry& registry, entt::entity entity) = 0;
        }; 

        template <typename Component>
        struct ComponentEventForwarder final : public Forwarder
        {
        public:
            ComponentEventForwarder(WorldContext& context) : m_context(context) {}
            ~ComponentEventForwarder() = default;

            void ForwardConstruct([[meybe_unused]]entt::registry& registry, entt::entity entity) override
            {
                ComponentEventBus::Event(GetTypeId<Component>(), &ComponentEventBus::Events::OnComponentConstruct, m_context, entity);
            }

            void ForwardUpdate([[meybe_unused]]entt::registry& registry, entt::entity entity) override
            {
                ComponentEventBus::Event(GetTypeId<Component>(), &ComponentEventBus::Events::OnComponentUpdate, m_context, entity);
            }

            void ForwardDestory([[meybe_unused]]entt::registry& registry, entt::entity entity) override
            {
                ComponentEventBus::Event(GetTypeId<Component>(), &ComponentEventBus::Events::OnComponentDestory, m_context, entity);
            }

        private:
            WorldContext& m_context;
        };

        entt::registry m_registry{};
        eastl::unordered_map<TypeId, eastl::unique_ptr<Forwarder>> m_forwarders;
    };
}