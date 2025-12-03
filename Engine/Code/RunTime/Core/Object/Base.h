#pragma once

#include <EASTL/intrusive_ptr.h>

namespace Spark
{
    template <typename T>
    using Ptr = eastl::intrusive_ptr<T>;

    template <typename T>
    using ConstPtr = eastl::intrusive_ptr<const T>;
}