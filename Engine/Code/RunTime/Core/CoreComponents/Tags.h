#pragma once

namespace Spark
{
    /// @brief Mark an entity will be destoryed
    struct DeadTag {};

    /// @brief Mark an entity is active
    struct ActiveTag{};

    /// @brief Mark an entity has been seleted
    struct SelectTag{};

    /// @brief This entity's lifetime belongs to a system, not to the scene: the system
    /// creates it, holds it, and destroys it (the editor camera, the default material).
    ///
    /// An ownership statement, and both behaviours follow from it -- it is not written to
    /// the scene file, and closing a scene does not take it. Works in any context.
    struct SystemOwnedTag {};
}