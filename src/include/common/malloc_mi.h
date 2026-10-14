#ifndef RGDB_COMMON_MALLOC_MI_H
#define RGDB_COMMON_MALLOC_MI_H

/*
 * Route the standard allocator names through mimalloc (C11).
 *
 * This used to be malloc_mi.hpp. In C++ the redirection worked by defining
 * macros that shadowed the libc declarations. In C the same trick works, but
 * it has to be applied *before* <stdlib.h> is ever included, otherwise the
 * macros would rewrite the declarations themselves and produce garbage
 * prototypes.
 *
 * To make that ordering automatic, this header includes the standard headers
 * first (with the macros still undefined) and only then installs the
 * redirections. Every translation unit that wants mimalloc-backed allocation
 * just includes this header first.
 *
 * <mimalloc.h> is itself a C header and already declares mi_malloc and
 * friends with C linkage, so no wrapper layer is needed.
 */

/*
 * When the build does not enable mimalloc (RGDB_USE_MIMALLOC=OFF) this header
 * degrades to a no-op: the standard allocator is already in effect via
 * <stdlib.h>, and the aligned helpers below fall back to the platform
 * primitives so that callers compile unchanged.
 */
#if defined(RGDB_USE_MIMALLOC)

#include <mimalloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef malloc
#undef malloc
#endif
#ifdef calloc
#undef calloc
#endif
#ifdef realloc
#undef realloc
#endif
#ifdef free
#undef free
#endif
#ifdef strdup
#undef strdup
#endif
#ifdef strndup
#undef strndup
#endif
#ifdef aligned_alloc
#undef aligned_alloc
#endif
#ifdef posix_memalign
#undef posix_memalign
#endif

#define malloc(size)                      mi_malloc(size)
#define calloc(count, size)               mi_calloc(count, size)
#define realloc(ptr, size)                mi_realloc(ptr, size)
#define free(ptr)                         mi_free(ptr)
#define strdup(str)                       mi_strdup(str)
#define strndup(str, n)                   mi_strndup(str, n)
#define aligned_alloc(align, size)        mi_aligned_alloc(align, size)
#define posix_memalign(ptr, align, size)  mi_posix_memalign(ptr, align, size)

/* mimalloc-specific helpers that have no standard-library equivalent. */
#define malloc_aligned(size, alignment)           mi_malloc_aligned(size, alignment)
#define calloc_aligned(count, size, alignment)    mi_calloc_aligned(count, size, alignment)
#define realloc_aligned(ptr, newsize, alignment)  mi_realloc_aligned(ptr, newsize, alignment)

/*
 * mi_free_aligned takes the alignment as a second argument (unlike plain
 * mi_free), because the aligned allocation carries a header that must be
 * stepped back over. Getting this arity wrong would silently corrupt the heap,
 * so the macro keeps the original two-argument shape.
 */
#define free_aligned(ptr, alignment)              mi_free_aligned(ptr, alignment)

/*
 * The original C++ header had a typo here:
 *
 *     #define realloc_aligned(p, newsize, alignment) mi_realloc_aligned(((p), (newsize), (alignment))
 *
 * That expanded to an unparenthesised comma expression missing a closing
 * paren, so it could never be used. It is fixed above.
 */

#else /* !RGDB_USE_MIMALLOC */

/*
 * Plain standard-library allocation. The aligned helpers are emulated with
 * _aligned_malloc / _aligned_free so that callers do not need to care which
 * allocator is active.
 *
 * Note the asymmetry, which mirrors mimalloc's own API and the POSIX
 * aligned_alloc contract: memory from an aligned allocator must go back to the
 * matching aligned free, never to plain free().
 */

#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define malloc_aligned(size, alignment) \
	_aligned_malloc((size), (alignment))
#define calloc_aligned(count, size, alignment) \
	_rgdb_calloc_aligned((count), (size), (alignment))
#define realloc_aligned(ptr, newsize, alignment) \
	_aligned_realloc((ptr), (newsize), (alignment))
#define free_aligned(ptr, alignment) \
	do { (void)(alignment); _aligned_free(ptr); } while (0)

/* _aligned_malloc does not zero, so calloc semantics are built by hand. */
static inline void* _rgdb_calloc_aligned(size_t count, size_t size, size_t alignment)
{
	size_t total = count * size;
	void* p = _aligned_malloc(total, alignment);
	if (p != NULL) {
		memset(p, 0, total);
	}
	return p;
}

#endif /* RGDB_USE_MIMALLOC */

#endif /* RGDB_COMMON_MALLOC_MI_H */
