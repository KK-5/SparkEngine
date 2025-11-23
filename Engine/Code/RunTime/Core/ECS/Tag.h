#pragma once

#include <entt/entt.hpp>

namespace Spark
{
    /// @brief Mark an entity will be destoryed
    struct DeadTag {};
    inline constexpr entt::exclude_t<DeadTag> ExcludeDeadTag{};
    inline constexpr entt::get_t<DeadTag> IncludeDeadTag{};

    /// @brief Mark an entity is active
    struct ActiveTag{};

    /// @brief Mark an entity has been seleted
    struct SelectTag{};
}

