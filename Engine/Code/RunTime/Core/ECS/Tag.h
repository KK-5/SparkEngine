#pragma once

#include <entt/entt.hpp>
#include <CoreComponents/Tags.h>

namespace Spark
{
    inline constexpr entt::exclude_t<DeadTag> ExcludeDeadTag{};
    inline constexpr entt::get_t<DeadTag> IncludeDeadTag{};
}
