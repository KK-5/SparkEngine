#pragma once

#include <entt/entt.hpp>

namespace Spark
{
    /// Base entity traits wrapper.
    /// Customize a concrete entity type by specializing this template.
    template<typename EntityType>
    struct EntityTraitsBase : public entt::entt_traits<EntityType>
    {
    };

    /// Primary entry for Spark entity traits.
    /// Project code should depend on EntityTraits instead of entt::entt_traits directly.
    template<typename EntityType>
    struct EntityTraits : public EntityTraitsBase<EntityType>
    {
    };

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
}
