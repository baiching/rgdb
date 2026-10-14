#ifndef RGDB_STORAGE_PAGE_ALLOCATOR_H
#define RGDB_STORAGE_PAGE_ALLOCATOR_H

/*
 * Page allocator (C11 port of page_allocator.hpp / PageAllocator).
 *
 * The C++ version was a singleton-ish class with static state: a global
 * `pages` buffer plus file-scope moodycamel queues and an atomic bump offset.
 * In C the class becomes a plain struct with functions, and the static state
 * becomes members so the allocator can actually be instantiated more than once.
 *
 * Type changes from C++ to C:
 *   - PageState enum class    -> enum + typedef
 *   - std::atomic<bool>       -> atomic_bool   (stdbool + stdatomic)
 *   - std::atomic<PageState>  -> atomic_int    (no atomic enum in C11)
 *   - std::atomic<size_t>     -> atomic_size_t
 *   - moodycamel::ConcurrentQueue<T> -> locked free-list stack (see below)
 *
 * The moodycamel lock-free queue is C++-only. The two queues in the original
 * (`clean_pages`, `dirty_pages`) were declared but never actually used by any
 * function, so they are replaced here by an SRWLOCK-guarded intrusive free
 * list that provides the same "recycle pages, track dirty pages" capability
 * with real semantics attached.
 */

/*
 * Atomic types.
 *
 * <stdatomic.h> is C11-only: MSVC ships it solely for C (and errors out if
 * included in C++ mode before C++23), so including it unconditionally would
 * make this header unusable from the C++ test suite. The shims below keep the
 * struct layout and the accessor names identical in both languages.
 *
 * In C the real C11 atomics are used. In C++ the members are declared as
 * std::atomic, which is layout-compatible for these trivially copyable
 * integer types and gives the same semantics.
 */
#ifdef __cplusplus

#include <atomic>

typedef std::atomic<bool>     rgdb_atomic_bool;
typedef std::atomic<int>      rgdb_atomic_int;
typedef std::atomic<size_t>   rgdb_atomic_size;

/* C++ std::atomic has no atomic_init; store is the equivalent for a new object. */
template <typename T, typename U>
inline void rgdb_atomic_init(T* obj, U value) { obj->store(value); }

#else /* C */

#include <stdatomic.h>

typedef atomic_bool     rgdb_atomic_bool;
typedef atomic_int      rgdb_atomic_int;
typedef atomic_size_t   rgdb_atomic_size;

#define rgdb_atomic_init(obj, value) atomic_init((obj), (value))

#endif /* __cplusplus */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/windows.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RGDB_PAGE_SIZE 8192
#define RGDB_TOTAL_PAGES 128
#define RGDB_TOTAL_BYTES ((size_t)RGDB_PAGE_SIZE * (size_t)RGDB_TOTAL_PAGES)

/* State of a page. */
typedef enum rgdb_page_state {
	RGDB_PAGE_CLEAN = 0,
	RGDB_PAGE_DIRTY,
	RGDB_PAGE_IN_USE
} rgdb_page_state;

/* What a page holds. */
typedef enum rgdb_page_type {
	RGDB_PAGE_TYPE_DATA = 0,
	RGDB_PAGE_TYPE_INDEX,
	RGDB_PAGE_TYPE_FREE
} rgdb_page_type;

typedef struct rgdb_page_header {
	rgdb_atomic_bool is_dirty;     /* mirrored in pstate; kept for parity */
	uint8_t*         page_ptr;     /* start of the usable page bytes */
	uint64_t         page_id;
	uint64_t         ref_count;    /* how many times handed out */
	uint64_t         last_accessed;/* monotonic counter, for eviction */
	uint64_t         page_type;    /* one of rgdb_page_type */
	rgdb_atomic_int  pstate;       /* one of rgdb_page_state */

	/* free-list linkage, used only while the page is recycled */
	struct rgdb_page_header* next_free;
} rgdb_page_header;

typedef struct rgdb_page_allocator {
	/* Backing arena. Owned by the allocator. */
	void*  pages;
	size_t total_bytes;

	/* Bump pointer: number of bytes handed out. */
	rgdb_atomic_size offset;

	/* Recycled pages (equivalent of the unused clean_pages queue). */
	rgdb_page_header* free_head;
	rgdb_rwlock_t     free_lock;

	/* Dirty pages awaiting flush (equivalent of dirty_pages queue). */
	rgdb_page_header** dirty;
	size_t             dirty_count;
	size_t             dirty_capacity;

	/* Statistics. */
	rgdb_atomic_size pages_allocated;
	rgdb_atomic_size pages_recycled;
	uint64_t         access_counter;

	/* Refuse to hand out more than this many pages. */
	size_t max_pages;
} rgdb_page_allocator;

/*
 * Pre-allocates RGDB_PAGE_SIZE * RGDB_TOTAL_PAGES bytes, aligned to the page
 * size. Returns false if the arena could not be allocated.
 */
bool rgdb_page_allocator_init(rgdb_page_allocator* alloc);

/* Releases the arena. Any page header handed out becomes invalid. */
void rgdb_page_allocator_deinit(rgdb_page_allocator* alloc);

/*
 * Hands out one page, or NULL when the arena is exhausted.
 *
 * Pages come from the recycled free list first, then from the bump pointer.
 * The returned header lives inside the arena immediately before its payload.
 */
rgdb_page_header* rgdb_page_allocator_get_page(rgdb_page_allocator* alloc);

/* Returns a page to the free list so a later get_page can reuse it. */
void rgdb_page_allocator_release_page(rgdb_page_allocator* alloc, rgdb_page_header* page);

/* Marks a page dirty and records it for a later flush. */
bool rgdb_page_allocator_mark_dirty(rgdb_page_allocator* alloc, rgdb_page_header* page);

/* Number of pages currently recorded as dirty. */
size_t rgdb_page_allocator_dirty_count(const rgdb_page_allocator* alloc);

/*
 * Clears the dirty list, marking every recorded page clean again.
 * Returns the number of pages flushed.
 */
size_t rgdb_page_allocator_flush(rgdb_page_allocator* alloc);

/* Number of pages handed out so far (excluding recycled reuse). */
size_t rgdb_page_allocator_pages_allocated(const rgdb_page_allocator* alloc);

/* Frees everything in one shot. Kept for parity with destroyPool(). */
bool rgdb_page_allocator_destroy_pool(rgdb_page_allocator* alloc);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RGDB_STORAGE_PAGE_ALLOCATOR_H */
