#pragma once

#include <cstdint>

#include <EASTL/type_traits.h>
#include <entt/entt.hpp>
#include <Math/Bit.h>

#include "Entity.h"
#include "EntityRefs.h"

namespace Spark
{
    enum class ComponentEvent : uint32_t
    {
        None = 0,
        Create,
        WillUpdate,
        Updated,
        Remove,
    };
    
    enum class ComponentEventMask : uint32_t
    {
        None        = 0,
        Create      = BIT(static_cast<uint32_t>(ComponentEvent::Create)),
        WillUpdate  = BIT(static_cast<uint32_t>(ComponentEvent::WillUpdate)),
        Updated     = BIT(static_cast<uint32_t>(ComponentEvent::Updated)),
        Remove      = BIT(static_cast<uint32_t>(ComponentEvent::Remove)),
        All         = Create | WillUpdate | Updated | Remove
    };
    DEFINE_ENUM_BITWISE_OPERATORS(Spark::ComponentEventMask, uint32_t);


    //! Type-level reflection flags, mounted with .Traits(ComponentTraits<T>::flags).
    //!
    //! A single enum for all of them: entt keeps ONE user traits value per element (the
    //! high bits of one uint32), so a second type-level enum would share those bits.
    enum class ComponentFlags : uint8_t
    {
        None       = 0,
        Editable   = 1 << 0,   //!< the inspector's add-component list offers it
        Persistent = 1 << 1,   //!< it goes into the scene file
    };

    DEFINE_ENUM_BITWISE_OPERATORS(Spark::ComponentFlags, uint8_t);

    constexpr bool HasComponentFlag(ComponentFlags flags, ComponentFlags query)
    {
        return (flags & query) == query;
    }
    
    /// Inherits EnTT storage traits and holds Spark defaults. Fully specialize ComponentTraits by
    /// inheriting this type and overriding only the members you need (others stay at defaults).
    template<typename T, typename EntityType = Spark::Entity>
    struct ComponentTraitsBase : public entt::component_traits<T, EntityType>
    {
        static constexpr ComponentFlags flags = ComponentFlags::None;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::None;
        static constexpr auto entityRefs = EntityRefs<>;
    };

    /// Primary template; entity type is Spark::Entity via ComponentTraitsBase defaults.
    /// For a non-default entity type, specialize using ComponentTraitsBase<T, YourEntity>.
    template<typename T>
    struct ComponentTraits : public ComponentTraitsBase<T>
    {
    };

    /// @brief Pointers to the fields of one component that reference an entity of type E.
    ///
    /// Empty unless the component declares them; a const component yields const pointers.
    /// E picks the context, so a MaterialHandle field is not reachable through
    /// GetEntityRefs<Entity>. The caller decides what to do with them.
    template<typename E, typename Component>
    constexpr auto GetEntityRefs(Component& component)
    {
        using Traits = ComponentTraits<eastl::remove_cv_t<Component>>;
        return Traits::entityRefs.template Collect<E>(component);
    }

    /// Convenience macro for full specialization of ComponentTraits.
    /// Inherits ComponentTraitsBase automatically so you only write the overrides.
    ///
    /// Usage:
    ///   SPARK_COMPONENT_TRAITS(MyComponent,
    ///       static constexpr ComponentFlags flags = ComponentFlags::Editable;
    ///       static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    ///   )
    ///
    /// Only for components of the default entity type. A component of another context
    /// specializes by hand on ComponentTraitsBase<T, ThatEntity> -- see the material ones
    /// in Feature/Material/Components.h.
#define SPARK_COMPONENT_TRAITS(ComponentType, ...)                      \
    template<>                                                          \
    struct ComponentTraits<ComponentType> : ComponentTraitsBase<ComponentType> \
    {                                                                   \
        __VA_ARGS__                                                     \
    };

}