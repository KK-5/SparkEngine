#pragma once

#include <cstdint>
#include <thread>
#include <functional>
#include <EASTL/functional.h>
#include <EASTL/intrusive_ptr.h>

namespace eastl
{
    //! 64-bit FNV-1a, folded over bytes: a hash of what they hold, not of where they are.
    inline constexpr uint64_t fnv1a64_seed = 14695981039346656037ull;

    inline void fnv1a64_fold(uint64_t& hash, const void* data, size_t size)
    {
        constexpr uint64_t prime = 1099511628211ull;
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<uint64_t>(bytes[i]);
            hash *= prime;
        }
    }

    inline uint64_t hash_bytes(const void* data, size_t size)
    {
        uint64_t hash = fnv1a64_seed;
        fnv1a64_fold(hash, data, size);
        return hash;
    }

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