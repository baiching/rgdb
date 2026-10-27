#include "storage/pageCache.h"
#include "common/logger.h"
#include <stdbool.h>
#include <assert.h>
#include <memory.h>
#include "common/c89atomic.h"

struct PageCache
{
	PageHeader *pageDirtyHead, *pageDirtyTail;
	PageHeader* pageSynced;								       /* last synced page in dirty list */
	uint32_t numOfRefCount;								       /* num of pages that are pinned */
	size_t		cacheSize;
	size_t		spillSize;								/* size before the spill */
	size_t		pageSize;								/* size of every page in this cache */
	size_t		extraSize;								/* size of extra space */
	bool		purgable;								/* true if pages are backed in store */
};

#define PAGE_CACHE_WALK_MAX 1000

static void print_cache(PageCache* pc) {
	PageHeader* oldp = pc->pageDirtyHead;
	PageHeader* newp = pc->pageDirtyTail;

	if (pc == NULL) {
		LOG("PageCache is null.");
		return;
	}

	LOG("PageCache: cache=%zu spill=%zu page=%zu extra=%zu refs=%zu purgable=%s",
		pc->cacheSize, pc->spillSize,
		pc->pageSize, pc->extraSize,
		pc->numOfRefCount,
		pc->purgable ? "yes" : "no");

	if (oldp == NULL) {
		LOG("Dirty list empty");
		return;
	}

	bool complete = true;
	size_t idx = 0;

	for (const PageHeader* p = oldp; p != NULL; p = p->pageDirtyNext) {
		const char* state;
		
		if (p == pc->pageSynced) {
			state = "dirty";
		}
		else
		{
			state = "synched";
		}

		LOG("%s", state);

		if (++idx >= PAGE_CACHE_WALK_MAX) {
			LOG("  ... truncated at %zu nodes (cycle or runaway)",
				(size_t)PAGE_CACHE_WALK_MAX);
			complete = false;
			break;
		}
	}

	if (complete && pc->pageSynced != NULL)
		LOG("  warning: pageSynced not reached — corrupt list?");

	LOG("  %zu page(s) in dirty list, oldest -> newest", idx);
}

#define PAGECACHE_DIRTYLIST_REMOVE	1	
#define PAGECACHE_DIRTYLIST_ADD		2
#define PAGECACHE_DIRTYLIST_FRONT	3

/* bits of addRemove determines the action*/
static void pageCache_dirtyListManager(PageHeader *page, uint8_t addRemove) {
	PageCache *pc = page->pageCache;

	LOG("%pc.DIRTYLIST. %s %d\n", pc,
		addRemove == 1 ? "REMOVE" : addRemove == 2 ? "ADD" : "FRONT",
		page->pageId);

	if (addRemove & PAGECACHE_DIRTYLIST_REMOVE) {
		assert(page->pageDirtyNext || page == pc->pageDirtyTail);
		assert(page->pageDirtyPrev || page == pc->pageDirtyHead);

		/* updating sync status */
		if (pc->pageSynced == page) {
			pc->pageSynced = page->pageDirtyPrev;
		}

		if (page->pageDirtyNext) {
			page->pageDirtyNext->pageDirtyPrev = page->pageDirtyPrev;
		}
		else
		{
			assert(page == pc->pageDirtyTail);
			pc->pageDirtyTail = page->pageDirtyPrev;
		}

		if (page->pageDirtyPrev) {
			page->pageDirtyPrev->pageDirtyNext = page->pageDirtyNext;
		}
		else
		{
			assert(page == pc->pageDirtyHead);
			pc->pageDirtyHead = page->pageDirtyNext;

			assert(pc->purgable);
			if (pc->pageDirtyHead == 0){			
					assert(pc->purgable == false);

			}
		}
	}

	if (addRemove & PAGECACHE_DIRTYLIST_ADD) {
		page->pageDirtyPrev = 0;
		page->pageDirtyNext = pc->pageDirtyHead;

		if (page->pageDirtyNext) {
			assert(page->pageDirtyNext->pageDirtyPrev == 0);
			page->pageDirtyNext->pageDirtyPrev = page;
		}
		else
		{
			pc->pageDirtyTail = page;
			assert(pc->purgable);
			
		}
		pc->pageDirtyHead = page;

		/*
		* if pageSync is NULL and page has NEED_SYNC flag,
		* set pageSynced to point to it.
		* 
		*/
		if (!pc->pageSynced && (page->flags & PAGEHEADER_NEED_SYNC) == 0)
		{
			pc->pageSynced = page;
		}
	}

	print_cache(pc);

}

static void pageCacheUnpin(PageHeader* p) {
	if (p->pageCache->purgable) {
		LOG("%p.UNPIN %d\n", p->pageCache, p->pageId);
		
		p->flags = (p->flags & ~PAGEHEADER_PINNED) | PAGEHEADER_UNPINNED; /* removes pinned flag and adds unpinned flag */
		print_cache(p->pageCache);
	}
}

/* returns the cache size */
static size_t numofCachePages(PageCache* p) {
	return p->cacheSize / p->pageSize;
}

/* primary initialization with some defaults */
PageCache *rgdb_pageCache_initilize(void) {
	PageCache* p = calloc(1, sizeof(*p));

	p->cacheSize = 128 * 4096; /* 512 kb */
	p->extraSize = 128 * 256; /* 32 KB */
	p->numOfRefCount = 0;
	p->pageDirtyHead = NULL;
	p->pageDirtyTail = NULL;
	p->pageSize = 4096;			/* 4kb per page */
	p->pageSynced = NULL;
	p->purgable = false;
	p->spillSize = 10 * 4096;	/* 10 extra pages */

	return p;
}

void rgdb_pageCache_shutDown(PageCache *p) {
	/* we should clean the pages that this cache owns first 
	*  then the pages needs to freed
	* only after that the cache may shut down
	* otherwise we'll have a massive leak
	*/
	if (p == NULL) return;
	free(p);
}

int rgdb_pageCache_newPageCache(
	PageCache* p,
	uint32_t pageSize,
	uint32_t extraSize,
	bool bPurgable
) {

	memset(p, 0, sizeof(PageCache));
	p->pageSize = pageSize;
	p->extraSize = extraSize;
	p->purgable = bPurgable;

	return 0;
}

int rgdb_pageCache_setPageSize(PageCache* p, int psize) {

	p->pageSize = psize;
	assert(p->pageSize);

	return 0;
}

RgdbPage *rgdb_pageCache_fetch(PageCache *p, uint32_t pageid, int flag) {
  assert(p != 0);
  assert(flag == 3 || flag == 0);

  /* TODO : look for the page at hash table and linked list */
}
