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

/* claims an available frame */
static BufferDesc* allocFrame(Lru* pool, BufferTag* tag) {
    BufferDesc* b = clockNextVictim(pool); /* finding eviction victim */

    if (b == NULL) {
        LOG("No victim frames were found.\n");
        return NULL;
    }

    if (b->is_valid && b->is_dirty && flushFrame(b) != 0) {
        LOG("flushing failed.\n");
        return NULL;
    }

    resetFrame(b);
    b->tag = *tag; /* puting new tag values */
    b->is_valid = true;

    /* storage manager needs to be implemented */

    return b;
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


BufferDesc* lruGetDesciption(Lru* pool, int buf_id) {
    if (pool == NULL) return NULL;

    if (buf_id < 0 || buf_id >= rgdb_config_cacheBufferSz)
    {
        return NULL;
    }

    BufferDesc* buf = &pool->buffer[buf_id];

    /* return null if the slot is empty */
    if (buf->is_valid == false) {
        LOG("the slot %d is empty or doesn't exist on the cache.\n", buf_id);
        return NULL;
    }
    /* if buffer_id is legit */
    return &pool->buffer[buf_id];
}

void* lruRead(Lru* pool, BufferTag* tag) {
    BufferDesc* b;
    int buf_id;

    if (pool == NULL || tag == NULL) {
        LOG("lru or buffer tag is empty.\n");
        return NULL;
    }

    buf_id = lruLookUp(pool, tag);

    if (buf_id >= 0 && (&pool->buffer[buf_id])->is_valid == true) {
        b = &pool->buffer[buf_id];
    }
    else {
        b = allocFrame(pool, tag);

        if (b == NULL) {
            LOG("No frames are allocated.\n");
            return NULL;
        }
    }
    /* TODO: read from the disk */
    setRefBit(b, 1);

    return b->data;
}

