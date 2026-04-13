#pragma once

#include "Entity.h"

namespace Spark
{
    template<typename EntityType>
    class BasicWorldContext;

    using WorldContext = BasicWorldContext<Entity>;
}
