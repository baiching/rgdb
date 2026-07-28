#pragma once

#include <vector>
#include <any>
#include<cstdint>

namespace rgdb {

	class Bplustree {
	public:
		typedef enum {
			BPLUS_SUCCESS = 0,
			BPLUS_DUPLICATE_KEY,
			BPLUS_MEM_ALLOCATION_FAILURE,
			BPLUS_INVALID_ARGUMENT,
			BPLUS_UNKNOWN_ERROR
		} Bplus_state;

		typedef struct {
			int total_pairs;
			int height;
		} stats;

		Bplustree(uint32_t max_keys, bool enable_stats);
		~Bplustree();
		
		int compare_keys(Key key1, Key key2);

		void free_tree(bplustree* tree); // will be removed

		template<typename Key, typename Val>
		Bplus_state insert(const Key* key, Val value);

		template<typename Key>
		std::any find_value(const Key key);

		template<typename Key, typename Val>
		Bplus_state update(const Key* key, const Val val);

		template<typename Key>
		Bplus_state remove(const Key* key);

		std::vector<std::any> range(const Key* start, const Key* end);

	private:

		struct node
		{
			bool is_leaf;
			int total_stored_keys;

			std::vector<std::any> keys;
			std::vector<std::any> values;
			std::vector<node *> childrens; 

			node* next;
			node* prev;
		};

		struct bplus_structure
		{
			int total_pairs;
			int tree_height;
			bool enable_debug;
			int max_keys;
			int min_leaf_keys;
			int min_internal_node_keys;
			node* root;
		};

		
	};


}