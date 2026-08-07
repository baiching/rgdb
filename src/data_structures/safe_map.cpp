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

	template<typename Key, typename Value>
	std::optional<Value> SafeHashMap<Key, Value>::get(Key& key) const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);
		auto it = this->map.find(key);

		if (it != this->map.end())
		{
			return it.second;
		}

		return nullptr;
	}

	template<typename Key, typename Value>
	bool SafeHashMap<Key, Value>::erase(Key& key) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);

		auto res = this->map.erase(key);

		return res.second;
	}

	template<typename Key, typename Value>
	bool SafeHashMap<Key, Value>::contains(Key& key) {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);

		return this->map.find(key) != this->map.end();
	}

	template<typename Key, typename Value>
	size_t SafeHashMap<Key, Value>::size() const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);
		
		return this->map.size();
	}

	template<typename Key, typename Value>
	bool SafeHashMap<Key, Value>::empty() const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);

		return this->map.empty();
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::clear() {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);

		this->map.clear();
	}

	template<typename Key, typename Value>
	std::vector<Key> SafeHashMap<Key, Value>::get_all_keys() const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);
		std::vector<Key> res;
		res.reserve(this->map.size());

		for (const auto& pair : this->map) {
			res.push_back(pair.first);
		}

		return res;
	}

	template<typename Key, typename Value>
	std::vector<Value> SafeHashMap<Key, Value>::get_all_values() const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);
		std::vector<Value> res;
		res.reserve(this->map.size());

		for (const auto& pair : this->map) {
			res.push_back(pair.second);
		}

		return res;
	}
	
	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::for_each(std::function<void(const Key&, const Value&)> func) const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);

		for (const auto& pair : this->map) {
			func(pair.first, pair.second);
		}
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::for_each_mut(std::function<void(Key&, Value&)> func) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);

		for (auto& pair : this->map) {
			func(pair.first, pair.second);
		}
	}

	template<typename Key, typename Value>
	template<typename Func>
	auto SafeHashMap<Key, Value>::with_read_lock(Func func) const {
		std::shared_lock<std::shared_mutex> read_lock(this->lock);
		func(this->map);
	}

	template<typename Key, typename Value>
	template<typename Func>
	auto SafeHashMap<Key, Value>::with_write_lock(Func func) const {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		func(this->map);
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::reserve(size_t size) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		this->map.reserve(size);
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::rehash(size_t size) {
		std::unique_lock<std::shared_mutex> write_lock(this->lock);
		this->map.rehash(size);
	}

	template<typename Key, typename Value>
	void SafeHashMap<Key, Value>::swap(SafeHashMap& other) noexcept {
		if (this != &other) {
			std::scoped_lock lock(this->lock, other.lock);
			this->map.swap(other.map);
		}
		
	}
}