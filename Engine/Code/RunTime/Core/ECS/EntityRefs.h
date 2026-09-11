#pragma once

#include <cstddef>

#include <EASTL/type_traits.h>

namespace Spark
{
    /// @brief Fixed array of the entity-reference fields found in one component.
    ///
    /// Not eastl::array: it declares `value_type mValue[N]`, and MSVC rejects a zero-length
    /// array. Most components reference no entity at all, so N == 0 is the common case.
    template<typename T, size_t N>
    struct EntityRefArray
    {
        T values[N];

        constexpr T* begin() { return values; }
        constexpr T* end() { return values + N; }
        constexpr const T* begin() const { return values; }
        constexpr const T* end() const { return values + N; }
        constexpr size_t size() const { return N; }
    };

    template<typename T>
    struct EntityRefArray<T, 0>
    {
        constexpr T* begin() const { return nullptr; }
        constexpr T* end() const { return nullptr; }
        constexpr size_t size() const { return 0; }
    };

    namespace Internal
    {
        template<typename Component, auto Member>
        using EntityRefFieldType =
            eastl::remove_cv_t<eastl::remove_reference_t<decltype(eastl::declval<Component&>().*Member)>>;

        template<typename E, typename Component, auto Member>
        constexpr bool IsEntityRefOf()
        {
            return eastl::is_same<EntityRefFieldType<Component, Member>, E>::value;
        }

        template<typename E, auto Member, typename Component, typename Array>
        constexpr void AppendEntityRef(Component& component, Array& refs, size_t& index)
        {
            if constexpr (IsEntityRefOf<E, Component, Member>())
            {
                refs.values[index] = &(component.*Member);
                ++index;
            }
        }
    }

    /// @brief The entity-reference fields a component declares, as member pointers.
    ///
    /// A declaration only: it states which bytes of a component hold an entity id, and the
    /// field's own type states which context that id belongs to. What to do with them is the
    /// caller's business -- merge rewrites them, a reader only looks.
    template<auto... Members>
    struct EntityRefList
    {
        template<typename E, typename Component>
        static constexpr size_t CountOf()
        {
            return (size_t{0} + ... + (Internal::IsEntityRefOf<E, Component, Members>() ? size_t{1} : size_t{0}));
        }

        template<typename E, typename Component>
        static constexpr auto Collect(Component& component)
        {
            using Ref = eastl::conditional_t<eastl::is_const<Component>::value, const E*, E*>;

            EntityRefArray<Ref, CountOf<E, Component>()> refs{};
            size_t index = 0;
            static_cast<void>(index);
            (Internal::AppendEntityRef<E, Members>(component, refs, index), ...);
            return refs;
        }
    };

    /// Spelling used in a component's traits: EntityRefs<&Hierarchy::parent, ...>.
    template<auto... Members>
    inline constexpr EntityRefList<Members...> EntityRefs{};
}
