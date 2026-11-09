#ifndef CLOCK_LRU_H
#define CLOCK_LRU_H

#include <stdbool.h>
#include <stdint.h>

#include "common/c89atomic.h"

#define BUFFER_POOL_SIZE 4 /* Pages in the buffer */

typedef struct BufferTag BufferTag;
typedef struct BufferDesc BufferDesc;
typedef struct Lru Lru;

struct BufferTag {
  uint64_t relNode; /* Table/relation id */
  uint64_t blockNum; /* block/page number */
};

struct BufferDesc {
  int buf_id;
  BufferTag tag;
  bool is_valid; /* true, if slot is not empty */
  bool is_dirty; /* true, if dirty */
  c89atomic_int64
      refBit; /* 0 means ready to evict, 1 means give a second chance */
  uint8_t *data;
};

struct Lru {
  BufferDesc buffer[BUFFER_POOL_SIZE];
  size_t clockHand;
  void *arena;
};

/* Initializes lru */
Lru *lruInit(void);
int lruDestroy(Lru *pool); /* Destroys the entire cache, returns  0 on success and -1 on failure */

/* buf_id of the page, or -1 if not resident. */
int lruLookup(const Lru *pool, const BufferTag *tag);

/* Descriptor for a frame. NULL if buf_id invalid. */
BufferDesc *lruGetDescription(Lru *pool, int buf_id);

/* Page data for a resident page, or NULL. Bumps ref_bit. */
void *lruRead (Lru *pool, const BufferTag *tag);
void *lruWrite(Lru *pool, const BufferTag *tag); /* marks dirty */

/* ---- eviction ----
 * it implement's the clock page replacement algorithm
 * if refBit 1 then it sets to 0 and moves on. if ref bit
 * is 0, it will evict the page immedietly.
 *
 * returns 0 on success, -1 on failure
 */
int lruEvict(Lru *pool);


void lruFlush     (Lru *pool, int buf_id); /* writes to the disk */
void lruFlush_all (Lru *pool);             /* writes the entire dirty pages in the cache */
void lruInvalidate(Lru *pool, int buf_id); /* drops the page from cache */
void lruInvalidateAll(Lru *pool);          /* drop all the pages from cache */

/* ---- debug ---- */
void lruPrintPool(const Lru *pool);


#endif
