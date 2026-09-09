#pragma once

namespace Spark
{
    /// @brief On a freshly created target entity: the source entity it was merged from.
    /// Doubles as the answer to "is this entity part of the batch being merged".
    template<typename EntityType>
    struct MergedFrom
    {
        EntityType source;
    };

    /// @brief On a target entity whose identifier a source entity wanted: where that source went.
    /// Only written on collision, so the mapping costs nothing when the target is near empty.
    template<typename EntityType>
    struct MergedTo
    {
        EntityType entity;
    };
}
