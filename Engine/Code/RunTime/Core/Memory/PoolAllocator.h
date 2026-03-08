#pragma once

#include "IAllocator.h"

#include <malloc.h>
#include <Math/Bit.h>

namespace Spark
{
    class PoolAllocatorBase
    {
    public:
        virtual void* allocate() = 0;
        virtual void deallocate(void* ptr) = 0;
    };

    template <typename T>
    class PoolAllocator : public IAllocator
                        , public PoolAllocatorBase
    {
    public:
        ////////////////////////////////
        // PoolAllocatorBase override
        T* allocate() override;
        void deallocate(T* ptr) override;
        /////////////////////////////////

    private:
        ////////////////////////////////
        // IAllocator override
        AllocateAddress* allocate(size_t n, int flags = 0) override;
		AllocateAddress* allocate(size_t n, size_t alignment, size_t offset, int flags = 0) override;
		void deallocate(void* ptr, size_t byteSize = 0) override;
        /////////////////////////////////

        static const size_t elementSize = sizeof(T);
    };

    template<typename T>
    AllocateAddress* PoolAllocator<T>::allocate(size_t n, [[maye_unused]]int flags)
    {
        void* ptr = malloc(n);
        return AllocateAddress{ptr, n};
    }

    template<typename T>
    AllocateAddress* PoolAllocator<T>::allocate(size_t n, size_t alignment, [[maye_unused]]size_t offset, [[maye_unused]]int flags)
    {
        size_t alignedSize = AlignUp(n, alignment);
        void* ptr = malloc(n, alignment);
        return AllocateAddress{ptr, alignedSize};
    }

    template<typename T>
    void PoolAllocator<T>::deallocate(void* ptr, [[maye_unused]]size_t byteSize)
    {
        free(ptr);
    }

    template<typename T>
    T* PoolAllocator<T>::allocate()
    {
        AllocateAddress* address = allocate(elementSize);
        if (address)
        {
            return (T*)address;
        }
        return nullptr;
    }

    template<typename T>
    void PoolAllocator<T>::deallocate(T* ptr)
    {
        deallocate(ptr);
    }
}