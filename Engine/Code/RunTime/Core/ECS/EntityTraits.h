#pragma once

#include <entt/entt.hpp>

namespace Spark
{
    template<typename EntityType>
    struct EntityTraits
    {
    };

    /*
    template<typename EntityType>
    [[nodiscard]] constexpr typename EntityTraits<EntityType>::entity_type ToIntegral(EntityType value) noexcept
    {
        return EntityTraits<EntityType>::to_integral(value);
    }

    template<typename EntityType>
    [[nodiscard]] constexpr typename EntityTraits<EntityType>::entity_type ToEntityPart(EntityType value) noexcept
    {
        return EntityTraits<EntityType>::to_entity(value);
    }

    template<typename EntityType>
    [[nodiscard]] constexpr typename EntityTraits<EntityType>::version_type ToVersionPart(EntityType value) noexcept
    {
        return EntityTraits<EntityType>::to_version(value);
    }

    template<typename EntityType>
    inline constexpr EntityType NullEntityV{entt::null};
    */
}
