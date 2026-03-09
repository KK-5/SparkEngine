#include "PoolAllocator.h"

#include <malloc.h>
#include <Math/Bit.h>

namespace Spark
{
    PoolAllocator::~PoolAllocator()
    {

    }

    AllocateAddress PoolAllocator::allocate(size_t n, [[maye_unused]]int flags)
    {
        void* ptr = malloc(n);
        return AllocateAddress{ptr, n};
    }

    AllocateAddress PoolAllocator::allocate(size_t n, size_t alignment, [[maye_unused]]size_t offset, [[maye_unused]]int flags)
    {
        size_t alignedSize = AlignUp(n, alignment);
        void* ptr = malloc(alignedSize);
        return AllocateAddress{ptr, alignedSize};
    }

    void PoolAllocator::deallocate(void* ptr, [[maye_unused]]size_t byteSize)
    {
        free(ptr);
    }
}