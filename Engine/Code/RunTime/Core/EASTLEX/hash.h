#pragma once

#include <thread>
#include <functional>
#include <EASTL/functional.h>
#include <EASTL/intrusive_ptr.h>

namespace eastl
{
    template<>
    struct hash<std::thread::id> 
    {
        size_t operator()(const std::thread::id& id) const {
            return std::hash<std::thread::id>()(id);
        }
    };

    template <class T>
    constexpr void hash_combine(size_t& seed, T const& v)
    {
        hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    constexpr void hash_combine_raw(size_t& seed, const size_t& otherHash)
    {
        seed ^= otherHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    template <class T1, class T2, class... RestTypes>
    constexpr void hash_combine(size_t& seed, const T1& firstElement, const T2& secondElement, const RestTypes&... restElements)
    {
        hash_combine(seed, firstElement);
        hash_combine(seed, secondElement, restElements...);
    }

    template<typename T>
    struct hash<intrusive_ptr<T>>
    {
        size_t operator()(const intrusive_ptr<T>& ptr) const
        {
            return hash<T*>()(ptr.get());
        }
    };
}