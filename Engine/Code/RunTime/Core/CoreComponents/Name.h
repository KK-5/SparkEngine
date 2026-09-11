#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include <ECS/ComponentTraits.h>

struct Name
{
    Name() = default;
    Name(eastl::string_view _name): name(_name){}
    
    eastl::string name;
};

namespace Spark
{
    SPARK_COMPONENT_TRAITS(::Name,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
    )
}
