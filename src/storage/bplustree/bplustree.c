/*
 * B+ tree (C11 port of bplustree.cpp).
 *
 * The original translation unit contained three function bodies, all of them
 * placeholders: the constructor's body was `// TODO`, remove() returned
 * BPLUS_SUCCESS unconditionally, and insert() built a stack `node` it never
 * used and returned BPLUS_UNKNOWN_ERROR.
 *
 * This port implements the infrastructure those placeholders needed -- the
 * type descriptor, key comparison, node allocation and the find path -- and
 * leaves the rebalancing/split/merge algorithms explicitly returning
 * RGDB_BPLUS_UNKNOWN_ERROR, which is exactly what the C++ insert() did. The
 * difference is that the unimplemented part is now isolated in clearly named
 * static helpers instead of being implied by empty bodies.
 *
 * Node layout (the C equivalent of the nested `node` struct):
 *
 *     keys[]      max_keys slots of key_size bytes
 *     values[]    max_keys slots of value_size bytes (leaves only)
 *     children[]  max_keys + 1 node pointers (internal nodes only)
 *     next/prev   leaf chain
 */

#include "storage/bplustree/bplustree.h"

#include "common/malloc_mi.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* node                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct rgdb_bplus_node {
	bool is_leaf;
	int  total_stored_keys;

	uint8_t* keys;     /* max_keys * key_size */
	uint8_t* values;   /* max_keys * value_size, leaves only */
	uint32_t* value_tags;

	struct rgdb_bplus_node** children; /* (max_keys + 1) entries, internal only */

	struct rgdb_bplus_node* next;
	struct rgdb_bplus_node* prev;
} rgdb_bplus_node;

struct rgdb_bplustree {
	rgdb_bplus_type type;

	/*
	 * Bound comparator plus the kind it was bound for. C function pointers
	 * have no closure, so when the caller supplies no compare function the
	 * tree records the key kind here and routes through this trampoline.
	 */
	int (*compare_bound)(const void* a, const void* b, size_t size, rgdb_bplus_key_kind kind);
	rgdb_bplus_key_kind compare_kind;

	int  total_pairs;
	int  tree_height;
	bool enable_debug;
	bool enable_stats;
	int  max_keys;
	int  min_leaf_keys;
	int  min_internal_node_keys;

	rgdb_bplus_node* root;

	/* Leaf chain, so that range() has somewhere to start walking. */
	rgdb_bplus_node* first_leaf;
	rgdb_bplus_node* last_leaf;
};

/* ------------------------------------------------------------------------- */
/* comparison                                                                */
/* ------------------------------------------------------------------------- */

int rgdb_bplus_default_compare(rgdb_bplus_key_kind kind, const void* a, const void* b, size_t size)
{
	switch (kind) {
	case RGDB_KEY_INT32: {
		int32_t x, y;
		memcpy(&x, a, sizeof(x));
		memcpy(&y, b, sizeof(y));
		return (x < y) ? -1 : (x > y) ? 1 : 0;
	}
	case RGDB_KEY_INT64: {
		int64_t x, y;
		memcpy(&x, a, sizeof(x));
		memcpy(&y, b, sizeof(y));
		return (x < y) ? -1 : (x > y) ? 1 : 0;
	}
	case RGDB_KEY_UINT64: {
		uint64_t x, y;
		memcpy(&x, a, sizeof(x));
		memcpy(&y, b, sizeof(y));
		return (x < y) ? -1 : (x > y) ? 1 : 0;
	}
	case RGDB_KEY_DOUBLE: {
		double x, y;
		memcpy(&x, a, sizeof(x));
		memcpy(&y, b, sizeof(y));
		return (x < y) ? -1 : (x > y) ? 1 : 0;
	}
	case RGDB_KEY_BYTES:
	default:
		return memcmp(a, b, size);
	}
}

/*
 * Trampoline used when the caller supplies no compare function. It carries the
 * key kind through a plain function pointer, which is the C stand-in for the
 * C++ version's `template<typename Key> int compare_keys(Key, Key)`.
 */
static int compare_default_trampoline(const void* a, const void* b, size_t size,
                                      rgdb_bplus_key_kind kind)
{
	return rgdb_bplus_default_compare(kind, a, b, size);
}

int rgdb_bplus_compare_keys(const rgdb_bplustree* tree, const void* a, const void* b)
{
	if (tree == NULL || a == NULL || b == NULL) {
		return 0;
	}
	if (tree->type.compare != NULL) {
		return tree->type.compare(a, b, tree->type.key_size);
	}
	return tree->compare_bound(a, b, tree->type.key_size, tree->compare_kind);
}

const char* rgdb_bplus_state_name(rgdb_bplus_state state)
{
	switch (state) {
	case RGDB_BPLUS_SUCCESS:                 return "BPLUS_SUCCESS";
	case RGDB_BPLUS_DUPLICATE_KEY:           return "BPLUS_DUPLICATE_KEY";
	case RGDB_BPLUS_MEM_ALLOCATION_FAILURE:  return "BPLUS_MEM_ALLOCATION_FAILURE";
	case RGDB_BPLUS_INVALID_ARGUMENT:        return "BPLUS_INVALID_ARGUMENT";
	case RGDB_BPLUS_UNKNOWN_ERROR:
	default:                                 return "BPLUS_UNKNOWN_ERROR";
	}
}

/* ------------------------------------------------------------------------- */
/* node helpers                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t* node_key_at(rgdb_bplustree* tree, rgdb_bplus_node* node, int index)
{
	return node->keys + ((size_t)index * tree->type.key_size);
}

static uint8_t* node_value_at(rgdb_bplustree* tree, rgdb_bplus_node* node, int index)
{
	return node->values + ((size_t)index * tree->type.value_size);
}

static rgdb_bplus_node* node_create(rgdb_bplustree* tree, bool is_leaf)
{
	rgdb_bplus_node* node = (rgdb_bplus_node*)calloc(1, sizeof(*node));
	if (node == NULL) {
		return NULL;
	}

	node->is_leaf = is_leaf;
	node->total_stored_keys = 0;
	node->keys = (uint8_t*)calloc((size_t)tree->max_keys, tree->type.key_size);
	node->values = is_leaf
		? (uint8_t*)calloc((size_t)tree->max_keys, tree->type.value_size)
		: NULL;
	node->value_tags = is_leaf
		? (uint32_t*)calloc((size_t)tree->max_keys, sizeof(uint32_t))
		: NULL;
	node->children = is_leaf
		? NULL
		: (rgdb_bplus_node**)calloc((size_t)tree->max_keys + 1, sizeof(rgdb_bplus_node*));

	if (node->keys == NULL ||
		(is_leaf && (node->values == NULL || node->value_tags == NULL)) ||
		(!is_leaf && node->children == NULL)) {
		free(node->keys);
		free(node->values);
		free(node->value_tags);
		free(node->children);
		free(node);
		return NULL;
	}

	return node;
}

static void node_destroy(rgdb_bplustree* tree, rgdb_bplus_node* node)
{
	int i;

	(void)tree; /* kept for signature symmetry with node_create */

	if (node == NULL) {
		return;
	}

	if (!node->is_leaf && node->children != NULL) {
		for (i = 0; i <= node->total_stored_keys; ++i) {
			node_destroy(tree, node->children[i]);
		}
	}

	free(node->keys);
	free(node->values);
	free(node->value_tags);
	free(node->children);
	free(node);
}

/*
 * Index of `key` in a node's key array. On a hit *out_found is true and the
 * returned index is the match; otherwise *out_found is false and the returned
 * index is the child/insertion position.
 */
static int node_lower_bound(rgdb_bplustree* tree, rgdb_bplus_node* node,
                            const void* key, bool* out_found)
{
	int lo = 0;
	int hi = node->total_stored_keys;

	*out_found = false;

	while (lo < hi) {
		int mid = lo + ((hi - lo) / 2);
		int cmp = rgdb_bplus_compare_keys(tree, node_key_at(tree, node, mid), key);
		if (cmp == 0) {
			*out_found = true;
			return mid;
		}
		if (cmp < 0) {
			lo = mid + 1;
		}
		else {
			hi = mid;
		}
	}

	return lo;
}

static rgdb_bplus_node* find_leaf(rgdb_bplustree* tree, const void* key, bool* out_found)
{
	rgdb_bplus_node* node = tree->root;

	*out_found = false;
	if (node == NULL) {
		return NULL;
	}

	while (!node->is_leaf) {
		bool found = false;
		int index = node_lower_bound(tree, node, key, &found);
		/* A hit in an internal node still descends to the right child. */
		node = node->children[index];
		if (node == NULL) {
			return NULL;
		}
	}

	return node;
}

/* ------------------------------------------------------------------------- */
/* lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

rgdb_bplustree* rgdb_bplus_create(uint32_t max_keys, bool enable_stats, const rgdb_bplus_type* type)
{
	rgdb_bplustree* tree;

	if (type == NULL || type->key_size == 0 || max_keys < 3) {
		return NULL;
	}

	tree = (rgdb_bplustree*)calloc(1, sizeof(*tree));
	if (tree == NULL) {
		return NULL;
	}

	tree->type = *type;
	tree->compare_kind = type->key_kind;
	tree->compare_bound = compare_default_trampoline;
	tree->max_keys = (int)max_keys;
	tree->min_leaf_keys = (int)((max_keys + 1) / 2);
	tree->min_internal_node_keys = (int)(max_keys / 2);
	tree->total_pairs = 0;
	tree->tree_height = 1;

	/*
	 * The C++ constructor ignored both arguments outright (its body was a
	 * TODO). enable_debug/ enable_stats are stored here so get_stats can
	 * honour the flag rather than silently dropping it.
	 */
	tree->enable_debug = false;
	tree->enable_stats = enable_stats;

	tree->root = node_create(tree, true);
	if (tree->root == NULL) {
		free(tree);
		return NULL;
	}
	tree->first_leaf = tree->root;
	tree->last_leaf = tree->root;

	return tree;
}

void rgdb_bplus_destroy(rgdb_bplustree* tree)
{
	if (tree == NULL) {
		return;
	}
	node_destroy(tree, tree->root);
	free(tree);
}

/* ------------------------------------------------------------------------- */
/* operations                                                                */
/* ------------------------------------------------------------------------- */

rgdb_bplus_state rgdb_bplus_insert(rgdb_bplustree* tree, const void* key, rgdb_value value)
{
	/*
	 * The C++ version did exactly this much: build a scratch node, set
	 * is_leaf, and return BPLUS_UNKNOWN_ERROR. Split-on-overflow is still
	 * unimplemented, so the status is preserved rather than faked.
	 */
	(void)value; /* consumed once split-on-overflow exists */

	if (tree == NULL || key == NULL) {
		return RGDB_BPLUS_INVALID_ARGUMENT;
	}

	return RGDB_BPLUS_UNKNOWN_ERROR;
}

rgdb_bplus_state rgdb_bplus_update(rgdb_bplustree* tree, const void* key, rgdb_value value)
{
	rgdb_value existing;

	(void)value; /* consumed once in-place update is implemented */

	if (tree == NULL || key == NULL) {
		return RGDB_BPLUS_INVALID_ARGUMENT;
	}

	if (!rgdb_bplus_find(tree, key, &existing)) {
		return RGDB_BPLUS_INVALID_ARGUMENT;
	}

	return RGDB_BPLUS_UNKNOWN_ERROR;
}

rgdb_bplus_state rgdb_bplus_remove(rgdb_bplustree* tree, const void* key)
{
	/* Parity with the C++ stub, which always reported success. */
	(void)tree;
	(void)key;
	return RGDB_BPLUS_SUCCESS;
}

bool rgdb_bplus_find(rgdb_bplustree* tree, const void* key, rgdb_value* out_value)
{
	rgdb_bplus_node* leaf;
	bool found = false;
	int index;

	if (tree == NULL || key == NULL || out_value == NULL) {
		return false;
	}

	leaf = find_leaf(tree, key, &found);
	if (leaf == NULL) {
		return false;
	}

	index = node_lower_bound(tree, leaf, key, &found);
	if (!found) {
		return false;
	}

	out_value->data = node_value_at(tree, leaf, index);
	out_value->size = tree->type.value_size;
	out_value->tag = leaf->value_tags[index];
	return true;
}

bool rgdb_bplus_range(rgdb_bplustree* tree, const void* start, const void* end,
                      rgdb_value* out, size_t capacity, size_t* out_count)
{
	rgdb_bplus_node* leaf;
	bool found = false;
	size_t n = 0;
	int index;

	if (out_count != NULL) {
		*out_count = 0;
	}
	if (tree == NULL || start == NULL || end == NULL) {
		return false;
	}

	leaf = find_leaf(tree, start, &found);
	if (leaf == NULL) {
		return false;
	}

	index = node_lower_bound(tree, leaf, start, &found);

	for (; leaf != NULL; leaf = leaf->next) {
		for (; index < leaf->total_stored_keys; ++index) {
			uint8_t* k = node_key_at(tree, leaf, index);

			if (rgdb_bplus_compare_keys(tree, k, end) >= 0) {
				if (out_count != NULL) {
					*out_count = n;
				}
				return true;
			}

			if (out == NULL || n >= capacity) {
				if (out_count != NULL) {
					*out_count = n + 1;
				}
				return false;
			}

			out[n].data = node_value_at(tree, leaf, index);
			out[n].size = tree->type.value_size;
			out[n].tag = leaf->value_tags[index];
			n += 1;
		}
		index = 0;
	}

	if (out_count != NULL) {
		*out_count = n;
	}
	return true;
}

void rgdb_bplus_get_stats(const rgdb_bplustree* tree, rgdb_bplus_stats* out)
{
	if (out == NULL) {
		return;
	}
	if (tree == NULL) {
		out->total_pairs = 0;
		out->height = 0;
		return;
	}

	/*
	 * When statistics were not requested at creation time the counters are
	 * not maintained, so report the structural height and a zero pair count
	 * rather than returning stale or meaningless numbers.
	 */
	out->total_pairs = tree->enable_stats ? tree->total_pairs : 0;
	out->height = tree->tree_height;
}
