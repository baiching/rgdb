/*
 * Thread-safe hash map (C11 port of safe_map.cpp).
 *
 * Design notes
 * ------------
 * The C++ version wrapped std::unordered_map in a std::shared_mutex. C has no
 * generic hash map in its standard library, so this file implements an
 * open-addressing table with linear probing directly:
 *
 *   - `slots` is a power-of-two array of entries; an entry with key == NULL
 *     and value == NULL is empty.
 *   - tombstones are represented by an entry with key == NULL and a non-NULL
 *     value pointer, so that erasure does not break probe chains.
 *   - keys and values are owned heap copies, sized key_size / value_size.
 *
 * The load factor is capped at 3/4 to keep linear probing cheap; growth
 * doubles the table.
 *
 * All public entry points take the lock themselves. The internal helpers
 * (find_slot_locked, insert_locked, ...) assume the lock is already held, which
 * is what makes for_each / with_*_lock able to run user callbacks atomically.
 */

#include "data_structures/safe_map.h"

#include "common/malloc_mi.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* defaults                                                                  */
/* ------------------------------------------------------------------------- */

uint64_t rgdb_hash_bytes(const void* key, size_t key_size)
{
	/* FNV-1a, 64-bit. Stable across runs, unlike a salted hash. */
	const unsigned char* p = (const unsigned char*)key;
	uint64_t h = 14695981039346656037ULL;
	size_t i;

	for (i = 0; i < key_size; ++i) {
		h ^= (uint64_t)p[i];
		h *= 1099511628211ULL;
	}

	/* Never return 0 for a real key so that "empty" stays unambiguous. */
	return (h == 0) ? 1u : h;
}

bool rgdb_key_eq_bytes(const void* a, const void* b, size_t key_size)
{
	if (a == b) {
		return true;
	}
	if (a == NULL || b == NULL) {
		return false;
	}
	return memcmp(a, b, key_size) == 0;
}

/* ------------------------------------------------------------------------- */
/* internal helpers (lock must be held)                                      */
/* ------------------------------------------------------------------------- */

static bool slot_is_empty(const rgdb_safe_map* map, size_t index)
{
	return map->slots[index].key == NULL && map->slots[index].value == NULL;
}

static bool slot_is_tombstone(const rgdb_safe_map* map, size_t index)
{
	return map->slots[index].key == NULL && map->slots[index].value != NULL;
}

static uint64_t key_hash(const rgdb_safe_map* map, const void* key)
{
	return map->hash(key, map->key_size);
}

static bool keys_match(const rgdb_safe_map* map, const void* a, const void* b)
{
	return map->key_eq(a, b, map->key_size);
}

/*
 * Locates `key`. On success returns true and stores the slot index.
 * On failure returns false and stores the index of the first reusable slot
 * (an empty or tombstone slot) in *out_insert_at.
 */
static bool find_slot_locked(const rgdb_safe_map* map, const void* key,
                             size_t* out_index, size_t* out_insert_at)
{
	uint64_t h;
	size_t mask;
	size_t i;
	size_t first_free = (size_t)-1;

	if (map->capacity == 0) {
		if (out_insert_at != NULL) {
			*out_insert_at = (size_t)-1;
		}
		return false;
	}

	h = key_hash(map, key);
	mask = map->capacity - 1;
	i = (size_t)(h & mask);

	for (;;) {
		if (slot_is_empty(map, i)) {
			if (out_insert_at != NULL) {
				*out_insert_at = (first_free != (size_t)-1) ? first_free : i;
			}
			return false;
		}
		if (slot_is_tombstone(map, i)) {
			if (first_free == (size_t)-1) {
				first_free = i;
			}
		}
		else if (map->slots[i].hash == h && keys_match(map, map->slots[i].key, key)) {
			if (out_index != NULL) {
				*out_index = i;
			}
			return true;
		}

		i = (i + 1) & mask;
	}
}

/*
 * Allocates a table of `new_capacity` slots and re-inserts every live entry.
 * Tombstones are dropped, so this also compacts.
 */
static bool resize_locked(rgdb_safe_map* map, size_t new_capacity)
{
	rgdb_safe_map_entry* old_slots = map->slots;
	size_t old_capacity = map->capacity;
	rgdb_safe_map_entry* new_slots;
	size_t i;
	size_t mask;

	if (new_capacity < 8) {
		new_capacity = 8;
	}
	if (new_capacity < map->count * 2) {
		new_capacity = map->count * 2;
	}

	new_slots = (rgdb_safe_map_entry*)calloc(new_capacity, sizeof(rgdb_safe_map_entry));
	if (new_slots == NULL) {
		return false;
	}

	map->slots = new_slots;
	map->capacity = new_capacity;
	/* count is unchanged; the loop below re-places the same entries. */
	mask = new_capacity - 1;

	for (i = 0; i < old_capacity; ++i) {
		rgdb_safe_map_entry* e = &old_slots[i];
		size_t j;

		if (e->key == NULL) {
			/* empty or tombstone: any value pointer here is scratch, drop it */
			if (e->value != NULL) {
				free(e->value);
			}
			continue;
		}

		j = (size_t)(e->hash & mask);
		while (map->slots[j].key != NULL) {
			j = (j + 1) & mask;
		}
		map->slots[j] = *e;
	}

	free(old_slots);
	return true;
}

static bool grow_if_needed_locked(rgdb_safe_map* map)
{
	/* Keep load factor <= 3/4 of capacity. */
	if (map->capacity == 0) {
		return resize_locked(map, 8);
	}
	if ((map->count + 1) * 4 >= map->capacity * 3) {
		return resize_locked(map, map->capacity * 2);
	}
	return true;
}

static bool insert_locked(rgdb_safe_map* map, const void* key, const void* value,
                          bool overwrite)
{
	size_t index = 0;
	size_t insert_at = 0;
	unsigned char* key_copy;
	unsigned char* value_copy;

	if (find_slot_locked(map, key, &index, &insert_at)) {
		if (!overwrite) {
			return false;
		}
		if (map->value_size > 0) {
			value_copy = (unsigned char*)malloc(map->value_size);
			if (value_copy == NULL) {
				return false;
			}
			memcpy(value_copy, value, map->value_size);
			free(map->slots[index].value);
			map->slots[index].value = value_copy;
		}
		return true;
	}

	if (!grow_if_needed_locked(map)) {
		return false;
	}
	/* The grow may have moved things; recompute the target slot. */
	if (!find_slot_locked(map, key, &index, &insert_at) && insert_at == (size_t)-1) {
		return false;
	}

	key_copy = (unsigned char*)malloc(map->key_size);
	if (key_copy == NULL) {
		return false;
	}
	memcpy(key_copy, key, map->key_size);

	value_copy = NULL;
	if (map->value_size > 0) {
		value_copy = (unsigned char*)malloc(map->value_size);
		if (value_copy == NULL) {
			free(key_copy);
			return false;
		}
		memcpy(value_copy, value, map->value_size);
	}

	/* Reclaim any tombstone scratch value before overwriting the slot. */
	if (map->slots[insert_at].value != NULL && map->slots[insert_at].key == NULL) {
		free(map->slots[insert_at].value);
	}

	map->slots[insert_at].key = key_copy;
	map->slots[insert_at].value = value_copy;
	map->slots[insert_at].hash = key_hash(map, key);
	map->count += 1;
	return true;
}

/* ------------------------------------------------------------------------- */
/* lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

bool rgdb_safe_map_init(rgdb_safe_map* map, size_t key_size, size_t value_size)
{
	if (map == NULL || key_size == 0) {
		return false;
	}

	memset(map, 0, sizeof(*map));
	map->key_size = key_size;
	map->value_size = value_size;
	map->hash = rgdb_hash_bytes;
	map->key_eq = rgdb_key_eq_bytes;
	InitializeSRWLock(&map->lock);

	return resize_locked(map, 8);
}

void rgdb_safe_map_deinit(rgdb_safe_map* map)
{
	size_t i;

	if (map == NULL) {
		return;
	}

	AcquireSRWLockExclusive(&map->lock);

	if (map->slots != NULL) {
		for (i = 0; i < map->capacity; ++i) {
			free(map->slots[i].key);
			free(map->slots[i].value);
		}
		free(map->slots);
		map->slots = NULL;
	}
	map->capacity = 0;
	map->count = 0;

	ReleaseSRWLockExclusive(&map->lock);
}

void rgdb_safe_map_set_hash(rgdb_safe_map* map, rgdb_hash_fn hash)
{
	if (map == NULL || hash == NULL) {
		return;
	}
	AcquireSRWLockExclusive(&map->lock);
	map->hash = hash;
	ReleaseSRWLockExclusive(&map->lock);
}

void rgdb_safe_map_set_key_eq(rgdb_safe_map* map, rgdb_key_eq_fn key_eq)
{
	if (map == NULL || key_eq == NULL) {
		return;
	}
	AcquireSRWLockExclusive(&map->lock);
	map->key_eq = key_eq;
	ReleaseSRWLockExclusive(&map->lock);
}

/* ------------------------------------------------------------------------- */
/* element access                                                            */
/* ------------------------------------------------------------------------- */

bool rgdb_safe_map_insert(rgdb_safe_map* map, const void* key, const void* value)
{
	bool ok;

	if (map == NULL || key == NULL) {
		return false;
	}

	AcquireSRWLockExclusive(&map->lock);
	ok = insert_locked(map, key, value, true);
	ReleaseSRWLockExclusive(&map->lock);
	return ok;
}

bool rgdb_safe_map_try_insert(rgdb_safe_map* map, const void* key, const void* value)
{
	bool ok;

	if (map == NULL || key == NULL) {
		return false;
	}

	AcquireSRWLockExclusive(&map->lock);
	ok = insert_locked(map, key, value, false);
	ReleaseSRWLockExclusive(&map->lock);
	return ok;
}

bool rgdb_safe_map_get(const rgdb_safe_map* map, const void* key, void* out_value)
{
	bool found = false;
	size_t index = 0;

	if (map == NULL || key == NULL) {
		return false;
	}

	/*
	 * The lock is a mutable member in spirit. SRWLOCK is a plain struct, so
	 * the const qualifier on the map is cast away rather than duplicated;
	 * this mirrors the C++ version, where `lock` was `mutable` so that const
	 * member functions could take a shared lock.
	 */
	AcquireSRWLockShared((SRWLOCK*)&map->lock);

	if (map->capacity > 0) {
		found = find_slot_locked(map, key, &index, NULL);
		if (found && out_value != NULL && map->value_size > 0) {
			memcpy(out_value, map->slots[index].value, map->value_size);
		}
	}

	ReleaseSRWLockShared((SRWLOCK*)&map->lock);
	return found;
}

bool rgdb_safe_map_contains(const rgdb_safe_map* map, const void* key)
{
	return rgdb_safe_map_get(map, key, NULL);
}

bool rgdb_safe_map_erase(rgdb_safe_map* map, const void* key)
{
	bool found;
	size_t index = 0;

	if (map == NULL || key == NULL) {
		return false;
	}

	AcquireSRWLockExclusive(&map->lock);

	found = (map->capacity > 0) && find_slot_locked(map, key, &index, NULL);
	if (found) {
		unsigned char* tombstone;

		/*
		 * Clear the slot to a well-defined empty state *first*. Both the
		 * tombstone path below and the resize fallback assume the old key
		 * pointer is gone; leaving a dangling key behind would make
		 * resize_locked copy freed memory.
		 */
		free(map->slots[index].key);
		free(map->slots[index].value);
		map->slots[index].key = NULL;
		map->slots[index].value = NULL;
		map->slots[index].hash = 0;

		/*
		 * Leave a tombstone: key == NULL but value != NULL, so probe chains
		 * stay intact. A sentinel byte is allocated if value_size is 0 so
		 * the tombstone is still distinguishable from an empty slot.
		 *
		 * The allocation must succeed. If it fails, leaving value == NULL
		 * would make this slot look *empty*, which truncates the probe chain
		 * and makes live keys further along it unreachable.
		 */
		tombstone = (unsigned char*)malloc(map->value_size > 0 ? map->value_size : 1);
		if (tombstone == NULL) {
			/*
			 * Cannot allocate the sentinel. Rather than corrupt lookups,
			 * rebuild the table without this entry so no tombstone is
			 * needed at all. The slot is already empty, so resize_locked's
			 * compaction drops it correctly.
			 */
			map->count -= 1;
			(void)resize_locked(map, map->capacity);
			ReleaseSRWLockExclusive(&map->lock);
			return true;
		}

		map->slots[index].key = NULL;
		map->slots[index].value = tombstone;
		map->slots[index].hash = 0;
		map->count -= 1;
	}

	ReleaseSRWLockExclusive(&map->lock);
	return found;
}

/* ------------------------------------------------------------------------- */
/* bulk operations                                                           */
/* ------------------------------------------------------------------------- */

void rgdb_safe_map_clear(rgdb_safe_map* map)
{
	size_t i;

	if (map == NULL) {
		return;
	}

	AcquireSRWLockExclusive(&map->lock);
	for (i = 0; i < map->capacity; ++i) {
		free(map->slots[i].key);
		free(map->slots[i].value);
		map->slots[i].key = NULL;
		map->slots[i].value = NULL;
		map->slots[i].hash = 0;
	}
	map->count = 0;
	ReleaseSRWLockExclusive(&map->lock);
}

size_t rgdb_safe_map_size(const rgdb_safe_map* map)
{
	size_t n;

	if (map == NULL) {
		return 0;
	}

	AcquireSRWLockShared((SRWLOCK*)&map->lock);
	n = map->count;
	ReleaseSRWLockShared((SRWLOCK*)&map->lock);
	return n;
}

bool rgdb_safe_map_empty(const rgdb_safe_map* map)
{
	return rgdb_safe_map_size(map) == 0;
}

bool rgdb_safe_map_reserve(rgdb_safe_map* map, size_t size)
{
	bool ok = true;

	if (map == NULL) {
		return false;
	}

	AcquireSRWLockExclusive(&map->lock);
	/* Need capacity such that `size` entries stay under the 3/4 load factor. */
	if (size > 0) {
		size_t needed = ((size * 4) + 2) / 3;
		size_t capacity = 8;
		while (capacity < needed) {
			capacity *= 2;
		}
		if (capacity > map->capacity) {
			ok = resize_locked(map, capacity);
		}
	}
	ReleaseSRWLockExclusive(&map->lock);
	return ok;
}

bool rgdb_safe_map_rehash(rgdb_safe_map* map, size_t size)
{
	bool ok;
	size_t capacity = 8;

	if (map == NULL) {
		return false;
	}

	while (capacity < size) {
		capacity *= 2;
	}

	AcquireSRWLockExclusive(&map->lock);
	ok = resize_locked(map, capacity);
	ReleaseSRWLockExclusive(&map->lock);
	return ok;
}

bool rgdb_safe_map_copy_keys(const rgdb_safe_map* map, void* out, size_t capacity, size_t* out_count)
{
	size_t i;
	size_t n = 0;

	if (map == NULL) {
		return false;
	}

	AcquireSRWLockShared((SRWLOCK*)&map->lock);

	if (out == NULL || capacity < map->count) {
		if (out_count != NULL) {
			*out_count = map->count;
		}
		ReleaseSRWLockShared((SRWLOCK*)&map->lock);
		return false;
	}

	for (i = 0; i < map->capacity; ++i) {
		if (map->slots[i].key != NULL) {
			memcpy((unsigned char*)out + (n * map->key_size), map->slots[i].key, map->key_size);
			n += 1;
		}
	}

	if (out_count != NULL) {
		*out_count = n;
	}
	ReleaseSRWLockShared((SRWLOCK*)&map->lock);
	return true;
}

bool rgdb_safe_map_copy_values(const rgdb_safe_map* map, void* out, size_t capacity, size_t* out_count)
{
	size_t i;
	size_t n = 0;

	if (map == NULL) {
		return false;
	}

	AcquireSRWLockShared((SRWLOCK*)&map->lock);

	if (out == NULL || capacity < map->count) {
		if (out_count != NULL) {
			*out_count = map->count;
		}
		ReleaseSRWLockShared((SRWLOCK*)&map->lock);
		return false;
	}

	for (i = 0; i < map->capacity; ++i) {
		if (map->slots[i].key != NULL) {
			memcpy((unsigned char*)out + (n * map->value_size), map->slots[i].value, map->value_size);
			n += 1;
		}
	}

	if (out_count != NULL) {
		*out_count = n;
	}
	ReleaseSRWLockShared((SRWLOCK*)&map->lock);
	return true;
}

/* ------------------------------------------------------------------------- */
/* iteration                                                                 */
/* ------------------------------------------------------------------------- */

void rgdb_safe_map_for_each(const rgdb_safe_map* map, rgdb_safe_map_visit_fn fn, void* user)
{
	size_t i;

	if (map == NULL || fn == NULL) {
		return;
	}

	AcquireSRWLockShared((SRWLOCK*)&map->lock);
	for (i = 0; i < map->capacity; ++i) {
		if (map->slots[i].key != NULL) {
			fn(map->slots[i].key, map->slots[i].value, user);
		}
	}
	ReleaseSRWLockShared((SRWLOCK*)&map->lock);
}

void rgdb_safe_map_for_each_mut(rgdb_safe_map* map, rgdb_safe_map_visit_mut_fn fn, void* user)
{
	size_t i;

	if (map == NULL || fn == NULL) {
		return;
	}

	AcquireSRWLockExclusive(&map->lock);
	for (i = 0; i < map->capacity; ++i) {
		if (map->slots[i].key != NULL) {
			fn(map->slots[i].key, map->slots[i].value, user);
		}
	}
	ReleaseSRWLockExclusive(&map->lock);
}

void rgdb_safe_map_with_read_lock(const rgdb_safe_map* map, rgdb_safe_map_locked_fn fn, void* user)
{
	if (map == NULL || fn == NULL) {
		return;
	}

	AcquireSRWLockShared((SRWLOCK*)&map->lock);
	fn(map, user);
	ReleaseSRWLockShared((SRWLOCK*)&map->lock);
}

void rgdb_safe_map_with_write_lock(rgdb_safe_map* map, rgdb_safe_map_locked_fn fn, void* user)
{
	if (map == NULL || fn == NULL) {
		return;
	}

	AcquireSRWLockExclusive(&map->lock);
	fn(map, user);
	ReleaseSRWLockExclusive(&map->lock);
}

void rgdb_safe_map_swap(rgdb_safe_map* a, rgdb_safe_map* b)
{
	rgdb_safe_map_entry* slots;
	size_t capacity;
	size_t count;
	size_t key_size;
	size_t value_size;
	rgdb_hash_fn hash;
	rgdb_key_eq_fn key_eq;
	bool release_a_then_b;

	if (a == NULL || b == NULL || a == b) {
		return;
	}

	/*
	 * The C++ version used std::scoped_lock on both maps' mutexes, which
	 * deadlock-avoids via std::lock. SRWLOCK has no equivalent try-lock
	 * helper, so this acquires in address order to make the ordering
	 * globally consistent and therefore deadlock-free, and releases in the
	 * reverse order.
	 */
	if ((uintptr_t)a < (uintptr_t)b) {
		AcquireSRWLockExclusive(&a->lock);
		AcquireSRWLockExclusive(&b->lock);
		release_a_then_b = false;
	}
	else {
		AcquireSRWLockExclusive(&b->lock);
		AcquireSRWLockExclusive(&a->lock);
		release_a_then_b = true;
	}

	slots = a->slots;          a->slots = b->slots;               b->slots = slots;
	capacity = a->capacity;    a->capacity = b->capacity;         b->capacity = capacity;
	count = a->count;          a->count = b->count;               b->count = count;
	key_size = a->key_size;    a->key_size = b->key_size;         b->key_size = key_size;
	value_size = a->value_size; a->value_size = b->value_size;    b->value_size = value_size;
	hash = a->hash;            a->hash = b->hash;                 b->hash = hash;
	key_eq = a->key_eq;        a->key_eq = b->key_eq;             b->key_eq = key_eq;

	if (release_a_then_b) {
		ReleaseSRWLockExclusive(&a->lock);
		ReleaseSRWLockExclusive(&b->lock);
	}
	else {
		ReleaseSRWLockExclusive(&b->lock);
		ReleaseSRWLockExclusive(&a->lock);
	}
}
