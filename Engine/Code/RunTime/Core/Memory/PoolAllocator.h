#pragma once

#include "IAllocator.h"

namespace Spark
{
    class PoolAllocatorBase
    {
    public:
        virtual void* allocate() = 0;
        virtual void deallocate(void* ptr) = 0;
    };

    class PoolAllocator : public IAllocator
    {
    public:
        PoolAllocator() = default;
        ~PoolAllocator();

        ////////////////////////////////
        // IAllocator override
        AllocateAddress allocate(size_t n, int flags = 0) override;
		AllocateAddress allocate(size_t n, size_t alignment, size_t offset, int flags = 0) override;
		void deallocate(void* ptr, size_t byteSize = 0) override;
        /////////////////////////////////

        //static const size_t elementSize = sizeof(T);
    private:
        size_t elementSize {0};
    };
}