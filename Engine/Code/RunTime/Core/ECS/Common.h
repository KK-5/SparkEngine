#pragma once

#include "Entity.h"

namespace Spark
{
    template<typename EntityType>
    class BasicContext;

    template<typename EntityType>
    class ExecuteContext;

    template<typename EntityType>
    class ExecuteContextGuard;

    template <typename ContextType, typename TraitsType>
    class ContextReference;

    // Forward-declaration only. Include WorldContext.h to instantiate objects.
    using WorldContext = BasicContext<Entity>;

    using WorldExecuteContext = ExecuteContext<Entity>;
    using WorldExecuteContextGuard = ExecuteContextGuard<Entity>;

    template <typename TraitsType>
    using WorldContextReference = ContextReference<WorldContext, TraitsType>;
}
