#pragma once

#include <malloc.h>

#define ALIGNED_MALLOC(byteSize, alignment) _aligned_malloc(byteSize, alignment)
#define ALIGNED_FREE(pointer) _aligned_free(pointer)
#define ALIGNED_REALLOC(pointer, byteSize, alignment) _aligned_realloc(pointer, byteSize, alignment)
#define ALIGNED_MSIZE(pointer, alignment) _aligned_msize(pointer, alignment, 0)