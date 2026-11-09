#include "storage/clock_lru.h"
#include "../rgdb_sysconf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void resetFrame(BufferDesc *b) {
  b->is_valid = false;
  b->is_dirty = false;
  c89atomic_store_64(&b->refBit, 0);
  memset(&b->tag, 0, sizeof(b->tag));

  if (b->data != NULL) {
    memset(b->data, 0, rgdb_config_pageSize);
  }

} 

Lru *ruInit() {

  Lru *pool = calloc(1, sizeof(*pool));

  if (pool == NULL)
    return NULL;

  size_t arena_size = (size_t)rgdb_config_pageSize * rgdb_config_numOfPages;
  pool->arena = malloc(arena_size);

  if (pool->arena == NULL) {
    free(pool);
    return NULL;
  }

  for (int i = 0; i < rgdb_config_cacheBufferSz; i++) {
    pool->buffer[i].data = pool->arena + (size_t)i * rgdb_config_pageSize;
    pool->buffer[i].buf_id = i;
    resetFrame(&pool->buffer[i]);
  }

  pool->clockHand = 0;

  return pool;

}
