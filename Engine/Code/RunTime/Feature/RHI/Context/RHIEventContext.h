#pragma once

#include <ECS/BasicContext.h>
#include <ECS/ExecuteContext.h>

#include "RHIEvent.h"

namespace Spark::RHI
{
    //! What one system leaves for another to pick up later: the producer creates an entity
    //! and puts the data on it, the receiver takes it by view. An entity here is only a
    //! handle to that data, and its slot is reused, so a handle kept past the entity's
    //! destruction has to be dropped.
    using RHIEventContext = BasicContext<RHIEvent>;
}

namespace Spark::RHI
{
    using RHIEventExecuteContext = ExecuteContext<RHIEvent>;
    using RHIEventExecuteContextGuard = ExecuteContextGuard<RHIEvent>;
}
