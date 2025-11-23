#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

struct Name final
{
    Name() = default;
    Name(eastl::string_view name): m_name(name){}
    
    eastl::string m_name;
};