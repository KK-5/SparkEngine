#pragma once

namespace Spark
{
    /// @brief Mark an entity will be destoryed
    struct DeadTag {};

    /// @brief Mark an entity is active
    struct ActiveTag{};

    /// @brief Mark an entity has been seleted
    struct SelectTag{};
}