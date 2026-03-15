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

        uint64_t m_budgetInBytes = 0;
    };
}