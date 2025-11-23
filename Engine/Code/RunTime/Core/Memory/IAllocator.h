/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <cstdio>

struct AllocateAddress
{
    //! default an empty address object
    AllocateAddress() = default;

    //! constructs an allocate address out of a void* and size
    AllocateAddress(void* address, size_t size)
        : m_value(address)
        , m_size(size)
    {}


    //! default copy constructor and assignment operator
    AllocateAddress(const AllocateAddress&) = default;
    AllocateAddress& operator=(const AllocateAddress&) = default;

    void* GetAddress() const
    {
        return m_value;
    }

    size_t GetAllocatedBytes() const
    {
        return m_size;
    }

    //! implicit void* operator allows assigning
    //! an AllocateAddress directly to a void*
    operator void* () const
    {
        return m_value;
    }

    //! explicit T* operator which is used
    //! for explicit casting from an AllocateAddress
    //! to a T* pointer
    template <class T>
    explicit operator T* () const
    {
        return reinterpret_cast<T*>(m_value);
    }

    explicit operator bool() const
    {
        return m_value != nullptr;
    }

    //! Store allocated memory address
    void* m_value{};
    //! Stores the amount of bytes allocated
    size_t m_size{};
};