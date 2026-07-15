#pragma once

#include <ECS/BasicContext.h>
#include <ECS/ExecuteContext.h>

#include "MaterialHandle.h"

namespace Spark::Material
{
    using MaterialContext = BasicContext<MaterialHandle>;
}

namespace Spark::Material
{

    using MaterialExecuteContext = ExecuteContext<MaterialHandle>;
    using MaterialExecuteContextGuard = ExecuteContextGuard<MaterialHandle>;
}
