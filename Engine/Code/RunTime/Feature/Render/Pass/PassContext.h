#pragma once

#include <ECS/BasicContext.h>
#include <ECS/ExecuteContext.h>

#include "Pass.h"

namespace Spark::Render
{
    using PassContext = BasicContext<Pass>;

    using PassExecuteContext = ExecuteContext<Pass>;
    using PassExecuteContextGuard = ExecuteContextGuard<Pass>;
}