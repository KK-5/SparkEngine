#pragma once

#include "Pass.h"

#include <HashString/HashString.h>

namespace Spark::Render
{
    template<HashString::hash_type NameHash>
    struct PassTag
    {
        static constexpr HashString::hash_type value = NameHash;
    };
}

#define SPARK_PASS_TAG(name) \
    ::Spark::Render::PassTag<::Spark::HashString{name}.value()>
