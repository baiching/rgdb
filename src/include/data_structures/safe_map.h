#ifndef RGDB_DATA_STRUCTURES_SAFE_MAP_H
#define RGDB_DATA_STRUCTURES_SAFE_MAP_H

/*
 * Thread-safe hash map (C11 port of safe_map.hpp / SafeHashMap).
 *
 * C has no templates, so the C++ `SafeHashMap<Key, Value>` becomes a single
 * runtime-typed map keyed by bytes:
 *
 *   - keys   are copied into the map and compared with memcmp (key_size bytes)
 *   - values are copied into the map and compared/copied with memcpy
 *   - hashing is per byte -> rgdb_hash_bytes
 *
 * This preserves the observable behaviour of the C++ class (insert / try_insert
 * / get / erase / contains / size / empty / clear / reserve / rehash / swap /
 * for_each / for_each_mut / with_read_lock / with_write_lock) with the
 * difference that "not found" is reported through a bool out-parameter instead
 * of std::optional, which has no C equivalent.
 *
 * Locking: std::shared_mutex becomes an SRWLOCK. SRWLOCK is not recursive, so
 * for_each / with_read_lock pass an already-locked table to the callback and
 * must not call back into the map's own locking API.
 *
 * Ownership: rgdb_safe_map_deinit frees the table. It does not free memory
 * pointed to by stored values; those belong to the caller.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/windows.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t (*rgdb_hash_fn)(const void* key, size_t key_size);
typedef bool (*rgdb_key_eq_fn)(const void* a, const void* b, size_t key_size);

typedef struct rgdb_safe_map_entry {
	unsigned char* key;   /* NULL marks an empty slot */
	unsigned char* value;
	uint64_t       hash;
} rgdb_safe_map_entry;

typedef struct rgdb_safe_map {
	rgdb_safe_map_entry* slots;
	size_t               capacity;   /* always a power of two, or 0 */
	size_t               count;
	size_t               key_size;
	size_t               value_size;
	rgdb_hash_fn         hash;
	rgdb_key_eq_fn       key_eq;
	rgdb_rwlock_t        lock;
} rgdb_safe_map;

typedef void (*rgdb_safe_map_visit_fn)(const void* key, void* value, void* user);
typedef void (*rgdb_safe_map_visit_mut_fn)(void* key, void* value, void* user);

/* --- lifecycle ---------------------------------------------------------- */

/* Returns false only when allocation of the initial table fails. */
bool rgdb_safe_map_init(rgdb_safe_map* map, size_t key_size, size_t value_size);

/* frees the table; safe to call more than once, and safe on a zeroed struct */
void rgdb_safe_map_deinit(rgdb_safe_map* map);

/* --- key/value type configuration --------------------------------------- */

void rgdb_safe_map_set_hash(rgdb_safe_map* map, rgdb_hash_fn hash);
void rgdb_safe_map_set_key_eq(rgdb_safe_map* map, rgdb_key_eq_fn key_eq);

/* Convenience used for both keys and values: flat byte comparison. */
bool rgdb_key_eq_bytes(const void* a, const void* b, size_t key_size);

/* Default hash used when none is supplied (FNV-1a over the key bytes). */
uint64_t rgdb_hash_bytes(const void* key, size_t key_size);

/* --- element access ----------------------------------------------------- */

/* Copies value into the map, inserting or overwriting. Returns success. */
bool rgdb_safe_map_insert(rgdb_safe_map* map, const void* key, const void* value);

/* Like insert, but fails (returns false) if the key already exists. */
bool rgdb_safe_map_try_insert(rgdb_safe_map* map, const void* key, const void* value);

/*
 * Copies the stored value into *out_value and returns true, or returns false
 * if the key is absent. out_value may be NULL to test for presence only.
 */
bool rgdb_safe_map_get(const rgdb_safe_map* map, const void* key, void* out_value);

bool rgdb_safe_map_contains(const rgdb_safe_map* map, const void* key);
bool rgdb_safe_map_erase(rgdb_safe_map* map, const void* key);

/* --- bulk operations ---------------------------------------------------- */

void   rgdb_safe_map_clear(rgdb_safe_map* map);
size_t rgdb_safe_map_size(const rgdb_safe_map* map);
bool   rgdb_safe_map_empty(const rgdb_safe_map* map);

/* Grows the table so it can hold at least `size` entries without rehashing. */
bool rgdb_safe_map_reserve(rgdb_safe_map* map, size_t size);

/* Forces the table to exactly `size` slots (rounded up to a power of two). */
bool rgdb_safe_map_rehash(rgdb_safe_map* map, size_t size);

/*
 * Copies every key (or every value) out into a caller-provided buffer.
 *
 * C has no std::vector to return. Instead the caller supplies a buffer of
 * `capacity` elements of key_size (resp. value_size) bytes; *out_count
 * receives the number written. If the buffer is too small the call fails and
 * *out_count is set to the number of elements required. Passing
 * capacity == 0 and out == NULL performs a size query.
 */
bool rgdb_safe_map_copy_keys(const rgdb_safe_map* map, void* out, size_t capacity, size_t* out_count);
bool rgdb_safe_map_copy_values(const rgdb_safe_map* map, void* out, size_t capacity, size_t* out_count);

/* --- iteration ---------------------------------------------------------- */

/* Visits every entry while holding the shared (read) lock. */
void rgdb_safe_map_for_each(const rgdb_safe_map* map, rgdb_safe_map_visit_fn fn, void* user);

/* Visits every entry while holding the exclusive (write) lock. */
void rgdb_safe_map_for_each_mut(rgdb_safe_map* map, rgdb_safe_map_visit_mut_fn fn, void* user);

/*
 * Runs fn(map, user) with the table locked, without copying any entry.
 * This is the C replacement for with_read_lock / with_write_lock: the
 * std::function<void(table&)> callback becomes a plain function pointer.
 *
 * The map is passed to the callback with the lock already held. The callback
 * must NOT call back into any rgdb_safe_map_* function: SRWLOCK is not
 * recursive for the exclusive case, and taking a second shared lock while
 * holding one is also a misuse. Use the for_each / for_each_mut callbacks
 * instead if you only need to visit entries.
 */
typedef void (*rgdb_safe_map_locked_fn)(const rgdb_safe_map* view, void* user);

void rgdb_safe_map_with_read_lock(const rgdb_safe_map* map, rgdb_safe_map_locked_fn fn, void* user);
void rgdb_safe_map_with_write_lock(rgdb_safe_map* map, rgdb_safe_map_locked_fn fn, void* user);

/* Exchanges the contents of two maps of identical key/value sizes. */
void rgdb_safe_map_swap(rgdb_safe_map* a, rgdb_safe_map* b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RGDB_DATA_STRUCTURES_SAFE_MAP_H */
