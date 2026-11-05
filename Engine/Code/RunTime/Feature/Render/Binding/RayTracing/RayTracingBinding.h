#pragma once

#include <Pass/TagList.h>

namespace Spark::Render
{
    //! Shared TLAS bindings (space7). Tracing scopes declare storage buffer reads explicitly.
    struct RayTracingSceneBindingTag
    {
        using Uses = TagList<>;
    };
}
