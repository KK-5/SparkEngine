# pragma once

#include "Memory.h"

#include <cstdlib>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#if _WIN32
#include "../../Platform/Windows/RunTime/Core/Memory/Memory.h"
#endif

// 由于暂时没有实现自定义的allocator，这里使用eastl默认的allocator，默认的allocator要求实现下面这两个函数
// 这里没有使用内存对齐，因为默认的allocator释放内存时不考虑内存对齐情况，想要使用内存对齐机制，需要自己实现allocator

void* operator new[](size_t size, 
                     [[maybe_unused]] const char* pName, 
                     [[maybe_unused]] int flags, 
                     [[maybe_unused]] unsigned debugFlags, 
                     [[maybe_unused]] const char* file, 
                     [[maybe_unused]] int line)
{
#ifdef _DEBUG
    return _malloc_dbg(size, _NORMAL_BLOCK, file, line);
#else
    return std::malloc(size);
#endif
}

void* operator new[](size_t size, 
                     [[maybe_unused]] size_t alignment, 
                     [[maybe_unused]] size_t alignmentOffset, 
                     [[maybe_unused]] const char* pName, 
                     [[maybe_unused]] int flags, 
                     [[maybe_unused]] unsigned debugFlags, 
                     [[maybe_unused]] const char* file, 
                     [[maybe_unused]] int line)
{
#ifdef _DEBUG
    return _malloc_dbg(size, _NORMAL_BLOCK, file, line);
#else
    return std::malloc(size);
#endif
}