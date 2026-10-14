/*
 * Page allocator (C11 port of page_allocator.cpp).
 *
 * The original C++ implementation had a latent bug worth calling out, because
 * the port has to decide what to do about it:
 *
 *     old_offset = offset.load(...);
 *     do {
 *         if (old_offset + PAGE_SIZE > TOTAL_BYTES) return nullptr;
 *         new_offset = old_offset + PAGE_SIZE;
 *     } while (!offset.compare_exchange_weak(old_offset, new_offset, ...));
 *     page = (Header*)((uint8_t*)pages + new_offset);
 *
 * The CAS publishes `new_offset`, but the load result is never refreshed from
 * the arena base, and the header is written *at* new_offset, i.e. in the last
 * page slot of the reservation. So a single getPage() writes a PageHeader into
 * the final page and leaves the whole arena unreachable.
 *
 * The C port fixes the arithmetic: the CAS publishes the *end* of the
 * reservation and the header is carved from the *start* of it. Each page
 * therefore consumes sizeof(rgdb_page_header) + RGDB_PAGE_SIZE bytes, and the
 * exhaustion test accounts for the header rather than only the payload.
 */

#include "storage/page_allocator.h"

#include "common/malloc_mi.h"

#include <string.h>

/*
 * Bytes consumed by one page.
 *
 * The header is carved first and the payload must follow on a page boundary,
 * so the header is padded up to RGDB_PAGE_SIZE. A stride of exactly
 * sizeof(header) + PAGE_SIZE would leave every payload misaligned by
 * sizeof(header), which is what the smoke test checks for.
 */
#define RGDB_PAGE_HEADER_AREA \
	(((sizeof(rgdb_page_header) + (size_t)RGDB_PAGE_SIZE - 1) / (size_t)RGDB_PAGE_SIZE) * (size_t)RGDB_PAGE_SIZE)
#define RGDB_PAGE_STRIDE (RGDB_PAGE_HEADER_AREA + (size_t)RGDB_PAGE_SIZE)

/* ------------------------------------------------------------------------- */

bool rgdb_page_allocator_init(rgdb_page_allocator* alloc)
{
	if (alloc == NULL) {
		return false;
	}

	memset(alloc, 0, sizeof(*alloc));

	/*
	 * One extra stride of slack: the bump pointer publishes reservation ends,
	 * so the arena must be able to contain a final full stride.
	 */
	alloc->max_pages = RGDB_TOTAL_PAGES;
	alloc->total_bytes = (RGDB_PAGE_STRIDE * RGDB_TOTAL_PAGES) + RGDB_PAGE_STRIDE;

	/*
	 * mi_malloc_aligned wants a power-of-two alignment; RGDB_PAGE_SIZE is
	 * 8192, which qualifies. It also requires the size to be a multiple of
	 * the alignment, so round the arena up.
	 */
	alloc->total_bytes = ((alloc->total_bytes + RGDB_PAGE_SIZE - 1) / RGDB_PAGE_SIZE) * RGDB_PAGE_SIZE;

	alloc->pages = malloc_aligned(alloc->total_bytes, RGDB_PAGE_SIZE);
	if (alloc->pages == NULL) {
		alloc->total_bytes = 0;
		return false;
	}

	atomic_init(&alloc->offset, 0);
	atomic_init(&alloc->pages_allocated, 0);
	atomic_init(&alloc->pages_recycled, 0);
	InitializeSRWLock(&alloc->free_lock);
	alloc->free_head = NULL;
	alloc->dirty = NULL;
	alloc->dirty_count = 0;
	alloc->dirty_capacity = 0;
	alloc->access_counter = 0;

	return true;
}

void rgdb_page_allocator_deinit(rgdb_page_allocator* alloc)
{
	if (alloc == NULL) {
		return;
	}

	if (alloc->pages != NULL) {
		free_aligned(alloc->pages, RGDB_PAGE_SIZE);
		alloc->pages = NULL;
	}
	if (alloc->dirty != NULL) {
		free(alloc->dirty);
		alloc->dirty = NULL;
	}

	alloc->dirty_count = 0;
	alloc->dirty_capacity = 0;
	alloc->free_head = NULL;
	alloc->total_bytes = 0;
}

/* ------------------------------------------------------------------------- */

static rgdb_page_header* pop_free_page(rgdb_page_allocator* alloc)
{
	rgdb_page_header* page;

	AcquireSRWLockExclusive(&alloc->free_lock);
	page = alloc->free_head;
	if (page != NULL) {
		alloc->free_head = page->next_free;
		page->next_free = NULL;
	}
	ReleaseSRWLockExclusive(&alloc->free_lock);

	return page;
}

void rgdb_page_allocator_release_page(rgdb_page_allocator* alloc, rgdb_page_header* page)
{
	if (alloc == NULL || page == NULL) {
		return;
	}

	atomic_store_explicit(&page->pstate, RGDB_PAGE_CLEAN, memory_order_relaxed);
	atomic_store_explicit(&page->is_dirty, false, memory_order_relaxed);
	page->ref_count = 0;
	page->page_type = RGDB_PAGE_TYPE_FREE;

	AcquireSRWLockExclusive(&alloc->free_lock);
	page->next_free = alloc->free_head;
	alloc->free_head = page;
	ReleaseSRWLockExclusive(&alloc->free_lock);
}

rgdb_page_header* rgdb_page_allocator_get_page(rgdb_page_allocator* alloc)
{
	rgdb_page_header* page;
	size_t old_offset;
	size_t new_offset;
	size_t start = 0;
	uint8_t* base;

	if (alloc == NULL || alloc->pages == NULL) {
		return NULL;
	}

	/* Reuse a recycled page when one is available. */
	page = pop_free_page(alloc);
	if (page != NULL) {
		page->page_id = (uint64_t)(page - (rgdb_page_header*)alloc->pages);
		page->ref_count += 1;
		page->last_accessed = ++alloc->access_counter;
		page->page_type = RGDB_PAGE_TYPE_DATA;
		atomic_store_explicit(&page->pstate, RGDB_PAGE_IN_USE, memory_order_relaxed);
		atomic_fetch_add_explicit(&alloc->pages_recycled, 1, memory_order_relaxed);
		return page;
	}

	base = (uint8_t*)alloc->pages;

	/* Bump-allocate a fresh stride with a CAS loop. */
	old_offset = atomic_load_explicit(&alloc->offset, memory_order_acquire);
	for (;;) {
		/*
		 * Enforce the page budget first. Checking total_bytes alone would
		 * allow one page too many, because the arena carries an extra stride
		 * of slack so that a final full stride always fits.
		 */
		if ((old_offset / RGDB_PAGE_STRIDE) >= alloc->max_pages) {
			return NULL;
		}
		/* Reject when a full stride would not fit. */
		if (old_offset > alloc->total_bytes || RGDB_PAGE_STRIDE > alloc->total_bytes - old_offset) {
			return NULL;
		}

		start = old_offset;
		new_offset = old_offset + RGDB_PAGE_STRIDE;

		if (atomic_compare_exchange_weak_explicit(&alloc->offset, &old_offset, new_offset,
				memory_order_release, memory_order_relaxed)) {
			/* Carve the header from the start of the reservation. */
			page = (rgdb_page_header*)(base + start);
			break;
		}
		/* CAS failed: old_offset now holds the current value, so retry. */
	}

	memset(page, 0, sizeof(*page));
	/*
	 * The payload starts at the next page boundary after the header, so that
	 * every page pointer the allocator hands out is RGDB_PAGE_SIZE-aligned.
	 * The padding between the header and the payload is unused.
	 */
	page->page_ptr = base + start + RGDB_PAGE_HEADER_AREA;
	page->page_id = (uint64_t)(start / RGDB_PAGE_STRIDE);
	page->ref_count = 1;
	page->last_accessed = ++alloc->access_counter;
	page->page_type = RGDB_PAGE_TYPE_DATA;

	/*
	 * Use atomic_store, not atomic_init: the arena is reused across
	 * init/deinit cycles, and re-initialising an atomic object that has
	 * already been used is not a valid way to write to it.
	 */
	atomic_store_explicit(&page->is_dirty, false, memory_order_relaxed);
	atomic_store_explicit(&page->pstate, RGDB_PAGE_IN_USE, memory_order_relaxed);

	atomic_fetch_add_explicit(&alloc->pages_allocated, 1, memory_order_relaxed);

	return page;
}

bool rgdb_page_allocator_mark_dirty(rgdb_page_allocator* alloc, rgdb_page_header* page)
{
	if (alloc == NULL || page == NULL) {
		return false;
	}

	atomic_store_explicit(&page->is_dirty, true, memory_order_relaxed);
	atomic_store_explicit(&page->pstate, RGDB_PAGE_DIRTY, memory_order_relaxed);
	page->last_accessed = ++alloc->access_counter;

	AcquireSRWLockExclusive(&alloc->free_lock);

	if (alloc->dirty_count == alloc->dirty_capacity) {
		size_t new_capacity = (alloc->dirty_capacity == 0) ? 16 : alloc->dirty_capacity * 2;
		rgdb_page_header** grown =
			(rgdb_page_header**)realloc(alloc->dirty, new_capacity * sizeof(*grown));
		if (grown == NULL) {
			ReleaseSRWLockExclusive(&alloc->free_lock);
			return false;
		}
		alloc->dirty = grown;
		alloc->dirty_capacity = new_capacity;
	}

	alloc->dirty[alloc->dirty_count++] = page;

	ReleaseSRWLockExclusive(&alloc->free_lock);
	return true;
}

size_t rgdb_page_allocator_dirty_count(const rgdb_page_allocator* alloc)
{
	size_t n;

	if (alloc == NULL) {
		return 0;
	}

	AcquireSRWLockShared((SRWLOCK*)&alloc->free_lock);
	n = alloc->dirty_count;
	ReleaseSRWLockShared((SRWLOCK*)&alloc->free_lock);
	return n;
}

size_t rgdb_page_allocator_flush(rgdb_page_allocator* alloc)
{
	size_t i;
	size_t flushed;

	if (alloc == NULL) {
		return 0;
	}

	AcquireSRWLockExclusive(&alloc->free_lock);

	flushed = alloc->dirty_count;
	for (i = 0; i < alloc->dirty_count; ++i) {
		rgdb_page_header* page = alloc->dirty[i];
		if (page != NULL) {
			atomic_store_explicit(&page->is_dirty, false, memory_order_relaxed);
			/* A page still handed out stays IN_USE; a released one is CLEAN. */
			if (atomic_load_explicit(&page->pstate, memory_order_relaxed) == RGDB_PAGE_DIRTY) {
				atomic_store_explicit(&page->pstate, RGDB_PAGE_CLEAN, memory_order_relaxed);
			}
		}
	}
	alloc->dirty_count = 0;

	ReleaseSRWLockExclusive(&alloc->free_lock);
	return flushed;
}

size_t rgdb_page_allocator_pages_allocated(const rgdb_page_allocator* alloc)
{
	if (alloc == NULL) {
		return 0;
	}
	/*
	 * The cast strips const so the atomic can be read through the shared
	 * lock-free accessor. rgdb_atomic_size is _Atomic(size_t) in C, and the
	 * cast is to that same type, so no atomic qualifier is discarded.
	 */
	return atomic_load_explicit((rgdb_atomic_size*)&alloc->pages_allocated, memory_order_relaxed);
}

bool rgdb_page_allocator_destroy_pool(rgdb_page_allocator* alloc)
{
	if (alloc == NULL) {
		return false;
	}

	/*
	 * Flush first so that no page is left marked dirty once the arena goes
	 * away, matching the original comment ("flush everything before release,
	 * to ensure no data gets lost").
	 */
	rgdb_page_allocator_flush(alloc);
	rgdb_page_allocator_deinit(alloc);

	return true;
}
