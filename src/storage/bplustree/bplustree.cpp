#include "storage/bplustree/bplustree.hpp"
#include "common/windows.hpp"

namespace rgdb {

	Bplustree::Bplustree(uint32_t max_keys, bool enable_stats) {
		// TODO
	}

	template<typename Key>
	Bplus_state Bplustree::remove(const Key *key) {
		return BPLUS_SUCCESS;
	}

	template<typename Key, typename Val>
	Bplus_state Bplustree::insert(const Key* key, Val value) {
		Bplustree::node branch;
		branch.is_leaf = true;
		return BPLUS_UNKNOWN_ERROR;
	}
}