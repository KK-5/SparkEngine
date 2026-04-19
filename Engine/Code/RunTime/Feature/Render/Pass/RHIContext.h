#pragma once

#include <ECS/BasicContext.h>
#include <ECS/ExecuteContext.h>

namespace Spark::Render
{
    using RHIContext = BasicContext<uint32_t>;

    using RHIExecuteContext = ExecuteContext<uint32_t>;
    using RHIExecuteContextGuard = ExecuteContextGuard<uint32_t>;
}