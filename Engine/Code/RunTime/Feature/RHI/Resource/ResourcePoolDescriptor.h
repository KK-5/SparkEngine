#pragma once

#include <cstdint>
#include <mutex>
#include <Object/ObjectPool.h>

namespace Spark::RHI
{
    struct ResourcePoolDescriptor
    {
        ResourcePoolDescriptor() = default;
        virtual ~ResourcePoolDescriptor() = default;
    };
}