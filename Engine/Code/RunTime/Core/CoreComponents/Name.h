#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

struct Name
{
    Name() = default;
    Name(eastl::string_view _name): name(_name){}
    
    eastl::string name;
};