#pragma once

#include <cstddef>
#include <cstdint>

#include <entt/entt.hpp>

#include "EntityTraits.h"

namespace Spark
{
    enum class Entity : uint32_t
    {
    };

    template<>
    struct EntityTraits<Entity>
    {
        using value_type = Entity;
        using entity_type = uint32_t;
        using version_type = uint16_t;
        
        static constexpr entity_type entity_mask = 0xFFFFF;
        static constexpr entity_type version_mask = 0xFFF;
    };
}

namespace entt
{
    template<>
    struct entt_traits<Spark::Entity> : basic_entt_traits<Spark::EntityTraits<Spark::Entity>>
    {
        using base_type = basic_entt_traits<Spark::EntityTraits<Spark::Entity>>;
        static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
    };
}

namespace Spark
{
    inline constexpr Entity NullEntity{entt::null};
}