#include "data_structures/safe_map.hpp"

namespace rgdb {
	
	template<typename Key, typename Value>
	SafeHashMap<Key, Value>::SafeHashMap(std::unordered_map<Key, Value> hmap) : map(std::move(hmap)) {
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::insert(Key& key, const Value& value) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		this->map[key] = value;
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::insert(Key&& key, Value&& value) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		this->map[std::move(key)] = std::move(value);
	}

	template<typename Key, typename Value>
	template<typename... Args>
	void SafeHashMap<Key, Value>::emplace(Args... args) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		this->map.emplace(std::forward<Args>(args)...);
	}

	template<typename Key, typename Value>
	bool SafeHashMap<Key, Value>::try_insert(Key& key, const Value& value) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		auto res = this->map.insert({ key, value });
		return res.second;
	}
	
}