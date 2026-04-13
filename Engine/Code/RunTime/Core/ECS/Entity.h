#pragma once

#include <cstddef>
#include <cstdint>

#include <entt/entt.hpp>

#include "EntityTraits.h"

namespace Spark
{
    enum class Entity : std::uint32_t
    {
    };

    template<>
    struct EntityTraits<Entity> : public EntityTraitsBase<Entity>
    {
        using value_type = Entity;
        using entity_type = std::uint32_t;
        using version_type = std::uint16_t;
        
        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };

    inline constexpr Entity NullEntity{entt::null};
}