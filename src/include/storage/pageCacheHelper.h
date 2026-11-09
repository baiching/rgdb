#ifndef PAGECACHE_HELPER_H
#define PAGECACHE_HELPER_H

#include "common/c89atomic.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define C89THREAD_IMPLEMENTATION
#include "common/c89thread.h"

typedef struct PageCacheE PageCacheE;
typedef struct PageHeaderE PageHeaderE;
//typedef struct PageCacheE PageCacheE;
typedef struct PageGroup PageGroup;

struct PageHeaderE {

  uint32_t iKey;         /* key value or page numeber */
  uint32_t isAnchor;     /* part of pageGroup lru */
  PageHeaderE *next;     /* next in hash chain  */
  PageCacheE *cache;
  PageHeaderE *lruNext;  /* next in circular lru unpinned pages */
  PageHeaderE *lruPrev;  /* previous in lru of unpinned pages */
};

struct PageGroup {
  c89mtx_t *mtx;
  size_t maxPageSize;    /* maximum pages in purgable caches in this group */
  size_t minPageSize;    /* minimum pages in purgable caches in this group */
  uint32_t maxPinned;    /* maxPage + 10 - minPage */
  uint32_t numPurgable;  /* number of purgable pages  */
  PageHeaderE lru;
};

struct PageCacheE {
  PageGroup *pg;         /* ownership of the cache */
  uint32_t *pnPurgable;  /* Pointer to PageGroup->numPurgable */
  size_t pageSize;     /* database page where contents will be held  */
  size_t extraSize;    /* sizeof(MemPage) + sizeof(PageHeader) */
  size_t totalAllocSize; /* total size for the entire cache line */ 
  bool purgable;         /* true if the cache if purgable */
  uint32_t minPage;      /* minimum pages reserved */
  uint32_t maxPage;      /* Maxumum pages reserved */
  uint32_t iMaxKey;      /* Lagest key since truncate() */
  uint32_t pnPurgableDummy; /* pnPurgable points here if not used */

  /* Hash table items
   *
   *
   */
  c89atomic_uint32 numRecyclable; /* number of pages in lru list */
  c89atomic_uint32 numPages;      /* total number of pages in the hash table */
  c89atomic_uint32 numHashSlot;   /* num of hash slots in the hash table */
};
#endif
