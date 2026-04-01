#pragma once

#include <EASTL/intrusive_ptr.h>
#include <EASTL/unique_ptr.h>

namespace Spark
{
    template <typename T>
    using Ptr = eastl::intrusive_ptr<T>;

    template <typename T>
    using ConstPtr = eastl::intrusive_ptr<const T>;

    template <typename T, typename Deleter = eastl::default_delete<T>>
    using UniquePtr = eastl::unique_ptr<T, Deleter>;

    template <typename T, typename Deleter = eastl::default_delete<const T>>
    using ConstUniquePtr = eastl::unique_ptr<const T, Deleter>;
}