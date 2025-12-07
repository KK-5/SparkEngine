#pragma once

#include <cstdint>

namespace Spark::Render::RHI
{
    struct ResourcePoolDescriptor
    {
        ResourcePoolDescriptor() = default;
        virtual ~ResourcePoolDescriptor() = default;

        uint64_t m_budgetInBytes = 0;
    };
}