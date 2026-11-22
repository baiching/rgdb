#include "storage/clock_lru.h"
#include "../rgdb_sysconf.h"
#include "common/logger.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void setRefBit(BufferDesc *b, uint64_t v) {
  c89atomic_store_64(&b->refBit, v);
}

static void resetFrame(BufferDesc *b) {

  if (b == NULL)
    return;
  
  b->is_valid = false;
  b->is_dirty = false;
  setRefBit(b, 0);
  memset(&b->tag, 0, sizeof(b->tag));

  if (b->data != NULL) {
    memset(b->data, 0, rgdb_config_pageSize);
  }

} 

Lru *ruInit(void) {

  Lru *pool = calloc(1, sizeof(*pool));

  if (pool == NULL)
    return NULL;

  size_t arena_size = (size_t)rgdb_config_pageSize * rgdb_config_numOfPages;
  pool->arena = malloc(arena_size);

  if (pool->arena == NULL) {
    free(pool);
    return NULL;
  }

  memset(pool->arena, 0, arena_size);

  for (int i = 0; i < rgdb_config_cacheBufferSz; i++) {

    pool->buffer[i].buf_id = i;
    pool->buffer[i].data = (uint8_t *)pool->arena + (size_t)i * rgdb_config_pageSize;
    resetFrame(&pool->buffer[i]);
  }

  pool->clockHand = 0;

  return pool;
}

int lruDestroy(Lru *pool) {
  if (pool == NULL) {
    LOG("Lru is already null");
    return -1;
  }

  lruFlush_all(pool); /* if not empty flush it all first */

  /* cleaning up pages individually */
  /*
  for (int i = 0; i < rgdb_config_cacheBufferSz; i++){
    free(pool->buffer[i].data);
  }
  */
  free(pool->arena);

  free(pool); /* cleaning the whole thing */
  
  return 0;
}  

int lruLookUp(Lru *pool, BufferTag *tag) {

  if (pool == NULL || tag == NULL) {
    return -1;
  }

  for (int i = 0; i < rgdb_config_cacheBufferSz; i++) {
    BufferDesc *b = &pool->buffer[i];

    if (b->is_valid && b->tag.relNode == tag->relNode &&
        b->tag.blockNum == tag->blockNum) {
      return b->buf_id;
    }
}
  return -1;
}
