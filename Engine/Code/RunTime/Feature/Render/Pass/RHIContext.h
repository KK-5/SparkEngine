#pragma once

#include <ECS/BasicContext.h>
#include <ECS/ExecuteContext.h>

#include "RHIHandle.h"

namespace Spark::Render
{
    using RHIContext = BasicContext<RHIHandle>;

    using RHIExecuteContext = ExecuteContext<RHIHandle>;
    using RHIExecuteContextGuard = ExecuteContextGuard<RHIHandle>;
}