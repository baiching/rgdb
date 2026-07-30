#pragma once

#include <mimalloc.h>

#undef malloc
#undef free
#undef calloc
#undef realloc

#define malloc(size)					 mi_malloc(size)
#define calloc(count, size)				 mi_calloc(count, size)
#define realloc(ptr, size)				 mi_realloc(ptr, size)
#define free(ptr)						 mi_free(ptr)
#define strdup(str)						 mi_strdup(str)
#define strndup(str, n)					 mi_strndup(str, n)
#define aligned_alloc(align, size)		 mi_aligned_alloc(align, size)
#define posix_memalign(ptr, align, size) mi_posix_memalign(ptr, align, size)
