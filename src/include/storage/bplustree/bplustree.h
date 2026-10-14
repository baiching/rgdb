#ifndef RGDB_STORAGE_BPLUSTREE_BPLUSTREE_H
#define RGDB_STORAGE_BPLUSTREE_BPLUSTREE_H

/*
 * B+ tree (C11 port of bplustree.hpp / Bplustree).
 *
 * The C++ header was a class template over Key/Val with std::any for value
 * storage and std::vector for the node arrays. C11 has neither, so:
 *
 *   - templates      -> runtime key/value descriptors (rgdb_bplus_type)
 *   - std::any       -> rgdb_value, a tagged fat pointer (see below)
 *   - std::vector    -> growable arrays owned by each node
 *   - class + pimpl  -> opaque struct rgdb_bplustree with accessor functions
 *
 * rgdb_value is deliberately a *non-owning* view: it carries a pointer, a
 * length and a tag. The tree copies value bytes into its own storage using the
 * descriptor's value_size, so callers only need to keep the source alive for
 * the duration of the insert/update call.
 *
 * NOTE: the C++ implementation was a skeleton -- the constructor body was a
 * TODO, and insert/remove returned placeholder statuses without touching any
 * node. This port keeps the same surface area and the same "not implemented
 * yet" semantics for the tree algorithms, but the node/type machinery they
 * will be built on is implemented here rather than left implicit.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes. Mirrors the original Bplus_state enum. */
typedef enum rgdb_bplus_state {
	RGDB_BPLUS_SUCCESS = 0,
	RGDB_BPLUS_DUPLICATE_KEY,
	RGDB_BPLUS_MEM_ALLOCATION_FAILURE,
	RGDB_BPLUS_INVALID_ARGUMENT,
	RGDB_BPLUS_UNKNOWN_ERROR
} rgdb_bplus_state;

/* Ordering tag for keys. */
typedef enum rgdb_bplus_key_kind {
	RGDB_KEY_INT32 = 0,
	RGDB_KEY_INT64,
	RGDB_KEY_UINT64,
	RGDB_KEY_DOUBLE,
	RGDB_KEY_BYTES
} rgdb_bplus_key_kind;

/*
 * Describes how keys and values are laid out and compared.
 *
 * key_size / value_size are the byte widths of one element. For by-value keys
 * this is sizeof(int32_t) etc.; for RGDB_KEY_BYTES it is the length of the
 * fixed-width byte string being compared.
 */
typedef struct rgdb_bplus_type {
	rgdb_bplus_key_kind key_kind;
	size_t              key_size;
	size_t              value_size;

	/*
	 * Optional custom comparator. Returns <0, 0 or >0.
	 * When NULL, the comparison is derived from key_kind.
	 */
	int (*compare)(const void* a, const void* b, size_t size);
} rgdb_bplus_type;

/* A non-owning tagged view of a value, the C stand-in for std::any. */
typedef struct rgdb_value {
	const void* data;
	size_t      size;
	uint32_t    tag;   /* caller-defined type tag */
} rgdb_value;

/* Tree statistics. Mirrors the nested `stats` struct. */
typedef struct rgdb_bplus_stats {
	int total_pairs;
	int height;
} rgdb_bplus_stats;

typedef struct rgdb_bplustree rgdb_bplustree;

/* --- lifecycle ---------------------------------------------------------- */

/*
 * Creates a tree. `max_keys` is the node fan-out; `enable_stats` turns on the
 * counter maintenance that rgdb_bplus_get_stats reports.
 * Returns NULL on allocation failure or if `type` is NULL / has key_size 0.
 */
rgdb_bplustree* rgdb_bplus_create(uint32_t max_keys, bool enable_stats, const rgdb_bplus_type* type);

void rgdb_bplus_destroy(rgdb_bplustree* tree);

/* --- operations --------------------------------------------------------- */

rgdb_bplus_state rgdb_bplus_insert(rgdb_bplustree* tree, const void* key, rgdb_value value);
rgdb_bplus_state rgdb_bplus_update(rgdb_bplustree* tree, const void* key, rgdb_value value);
rgdb_bplus_state rgdb_bplus_remove(rgdb_bplustree* tree, const void* key);

/*
 * Looks up a key. On success the stored value view is written to *out_value
 * and true is returned; the view stays valid until the entry is modified.
 * This replaces `std::any find_value(...)`.
 */
bool rgdb_bplus_find(rgdb_bplustree* tree, const void* key, rgdb_value* out_value);

/*
 * Collects the entries in [start, end) into `out`, at most `capacity` of them.
 * *out_count receives the number written, or the number required when the
 * buffer is too small (in which case false is returned). This replaces
 * `std::vector<std::any> range(...)`, which had no C equivalent.
 */
bool rgdb_bplus_range(rgdb_bplustree* tree, const void* start, const void* end,
                      rgdb_value* out, size_t capacity, size_t* out_count);

/* Fills *out with the current statistics. */
void rgdb_bplus_get_stats(const rgdb_bplustree* tree, rgdb_bplus_stats* out);

/* Compares two keys using the tree's descriptor. Returns <0, 0 or >0. */
int rgdb_bplus_compare_keys(const rgdb_bplustree* tree, const void* a, const void* b);

/* Default comparator for a key kind, used when type->compare is NULL. */
int rgdb_bplus_default_compare(rgdb_bplus_key_kind kind, const void* a, const void* b, size_t size);

/* Human-readable name for a status code. */
const char* rgdb_bplus_state_name(rgdb_bplus_state state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RGDB_STORAGE_BPLUSTREE_BPLUSTREE_H */
