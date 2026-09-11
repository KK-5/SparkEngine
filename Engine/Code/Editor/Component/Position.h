#pragma once

#include <ECS/ComponentTraits.h>

namespace Editor
{
    struct Position
    {
        float x{0};
        float y{0};
        float z{0};
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Editor::Position,
        static constexpr ComponentFlags flags = ComponentFlags::Editable;
    )
}
