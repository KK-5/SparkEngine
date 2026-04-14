#pragma once

#include <EASTL/type_traits.h>
#include <entt/entt.hpp>
#include <Math/Bit.h>

#include "Entity.h"

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
    
    /// Inherits EnTT storage traits and holds Spark defaults. Fully specialize ComponentTraits by
    /// inheriting this type and overriding only the members you need (others stay at defaults).
    template<typename T, typename EntityType = Spark::Entity>
    struct ComponentTraitsBase : public entt::component_traits<T, EntityType>
    {
        static constexpr bool editable = false;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::None;
    };

    /// Primary template; entity type is Spark::Entity via ComponentTraitsBase defaults.
    /// For a non-default entity type, specialize using ComponentTraitsBase<T, YourEntity>.
    template<typename T>
    struct ComponentTraits : public ComponentTraitsBase<T>
    {};
    
}