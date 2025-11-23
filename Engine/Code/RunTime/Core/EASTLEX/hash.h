#pragma once

#include <thread>
#include <functional>
#include <EASTL/functional.h>

namespace eastl
{
    template<>
    struct hash<std::thread::id> {
        size_t operator()(const std::thread::id& id) const {
            return std::hash<std::thread::id>()(id);
        }
    };
}