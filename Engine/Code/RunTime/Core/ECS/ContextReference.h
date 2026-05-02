#pragma once

#include <EASTL/type_traits.h>

#include "BasicContext.h"
#include "SystemTraits.h"

namespace Spark
{
    namespace Details
    {
        template <typename ContextType, typename = void>
        struct ContextEntityType
        {
            using type = decltype(eastl::declval<ContextType&>().CreateEntity());
        };

        template <typename ContextType>
        struct ContextEntityType<ContextType, eastl::void_t<typename ContextType::Entity>>
        {
            using type = typename ContextType::Entity;
        };

        constexpr bool CheckAccess(ComponentAccess provided, ComponentAccess required) noexcept
        {
            return (static_cast<uint32_t>(provided) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
        }

        template <typename Component, ComponentAccess RequiredAccess, typename AcquireType>
        struct AcquireMatches : eastl::bool_constant<
            (eastl::is_same_v<typename AcquireType::component_type, eastl::remove_cv_t<Component>> ||
                eastl::is_same_v<typename AcquireType::component_type, All>) &&
            CheckAccess(AcquireType::access, RequiredAccess)>
        {
        };

        template <typename Component, ComponentAccess RequiredAccess, typename AcquireList>
        struct HasAccess;

        template <typename Component, ComponentAccess RequiredAccess, typename... AcquireTypes>
        struct HasAccess<Component, RequiredAccess, eastl::meta::type_list<AcquireTypes...>>
            : eastl::disjunction<AcquireMatches<Component, RequiredAccess, AcquireTypes>...>
        {
        };
    } // namespace Details

    // ContextReference exposes only the context APIs allowed by system acquires.
    // Expected usage:
    //   using Traits = Spark::SystemTraits<MySystem>;
    //   Spark::ContextReference<MyContext, Traits> ctxRef(context);
    // Compile-time checks example:
    //   static_assert(ContextRef::CanReadComponentV<Transform>);
    //   static_assert(!ContextRef::CanWriteComponentV<Transform>);
    template <typename ContextType, typename TraitsType>
    class ContextReference final
    {
    public:
        using Entity = typename Details::ContextEntityType<ContextType>::type;
        using Acquires = typename TraitsType::acquires;

        explicit ContextReference(ContextType& context) noexcept
            : m_context(context)
        {
        }

        ContextReference(const ContextReference&) = default;
        ContextReference& operator=(const ContextReference&) = default;

        template <typename Component>
        static inline constexpr bool CanReadComponentV =
            Details::HasAccess<Component, ComponentAccess::Read, Acquires>::value;

        template <typename Component>
        static inline constexpr bool CanWriteComponentV =
            Details::HasAccess<Component, ComponentAccess::Write, Acquires>::value;

        template <typename... Components>
        static inline constexpr bool CanReadAllV = (CanReadComponentV<Components> && ...);

        template <typename... Components>
        static inline constexpr bool CanWriteAllV = (CanWriteComponentV<Components> && ...);

        Entity CreateEntity()
        {
            return m_context.CreateEntity();
        }

        Entity CreateEntity(eastl::string_view name)
        {
            return m_context.CreateEntity(name);
        }

        void DestoryEntity(Entity entity)
        {
            m_context.DestoryEntity(entity);
        }

        bool Valid(Entity entity) const noexcept
        {
            return m_context.Valid(entity);
        }

        template <typename T, typename... Args>
        decltype(auto) Add(Entity entity, Args&&... args)
        {
            static_assert(CanWriteComponentV<T>,
                "ContextReference::Add<T>: write access required. Declare WriteComponent<T>, "
                "ReadWriteComponent<T>, WriteComponent<All>, or ReadWriteComponent<All>.");
            return m_context.template Add<T>(entity, eastl::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        decltype(auto) AddOrReplace(Entity entity, Args&&... args)
        {
            static_assert(CanWriteComponentV<T>,
                "ContextReference::AddOrReplace<T>: write access required. Declare WriteComponent<T>, "
                "ReadWriteComponent<T>, WriteComponent<All>, or ReadWriteComponent<All>.");
            return m_context.template AddOrReplace<T>(entity, eastl::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        decltype(auto) Repalce(Entity entity, Args&&... args)
        {
            static_assert(CanWriteComponentV<T>,
                "ContextReference::Repalce<T>: write access required. Declare WriteComponent<T>, "
                "ReadWriteComponent<T>, WriteComponent<All>, or ReadWriteComponent<All>.");
            return m_context.template Repalce<T>(entity, eastl::forward<Args>(args)...);
        }

        template <typename... T>
        decltype(auto) Get(Entity entity) const
        {
            static_assert(CanReadAllV<T...>,
                "ContextReference::Get<T...> const: read access required for all requested components. "
                "Declare ReadComponent/ReadWriteComponent (or All variants).");
            return m_context.template Get<T...>(entity);
        }

        template <typename... T>
        decltype(auto) Get(Entity entity)
        {
            static_assert(CanWriteAllV<T...>,
                "ContextReference::Get<T...>: mutable access requires write permission for all requested components. "
                "Declare WriteComponent/ReadWriteComponent (or All variants).");
            return m_context.template Get<T...>(entity);
        }

        template <typename Type, typename... Other>
        decltype(auto) Remove(Entity entity)
        {
            static_assert(CanWriteAllV<Type, Other...>,
                "ContextReference::Remove<T...>: write access required for all requested components.");
            return m_context.template Remove<Type, Other...>(entity);
        }

        template <typename T>
        bool Has(Entity entity) const
        {
            static_assert(CanReadComponentV<T>,
                "ContextReference::Has<T>: read access required.");
            return m_context.template Has<T>(entity);
        }

        template <typename... T>
        bool HasAny(Entity entity) const
        {
            static_assert(CanReadAllV<T...>,
                "ContextReference::HasAny<T...>: read access required for all requested components.");
            return m_context.template HasAny<T...>(entity);
        }

        template <typename... T>
        bool HasAll(Entity entity) const
        {
            static_assert(CanReadAllV<T...>,
                "ContextReference::HasAll<T...>: read access required for all requested components.");
            return m_context.template HasAll<T...>(entity);
        }

        template <typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{}) const
        {
            static_assert(CanReadAllV<Component...>,
                "ContextReference::GetView<T...> const: read access required for all requested components.");
            return m_context.template GetView<Component...>(excludes);
        }

        template <typename... Component, typename... Exclude>
        decltype(auto) GetView(entt::exclude_t<Exclude...> excludes = entt::exclude_t{})
        {
            static_assert(CanWriteAllV<Component...>,
                "ContextReference::GetView<T...>: mutable view requires write access for all requested components.");
            return m_context.template GetView<Component...>(excludes);
        }

        ContextType& GetContext() noexcept
        {
            return m_context;
        }

        const ContextType& GetContext() const noexcept
        {
            return m_context;
        }

    private:
        ContextType& m_context;
    };
}
