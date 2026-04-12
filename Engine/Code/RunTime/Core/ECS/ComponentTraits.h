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


    template<typename T, typename EntityType = Spark::Entity>
    struct ComponentTraits : public entt::component_traits<T, EntityType>
    {
        static constexpr bool editable = false;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::None;
    };
    
}