#pragma once
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <vector>
#include <functional>

namespace rgdb {

	template<typename Key, typename Value>
	class SafeHashMap {
	private:
		std::unordered_map<Key, Value> map;
		std::shared_mutex lock;

	public:
		SafeHashMap(std::unordered_map<Key, Value> map);

		void insert(Key& key, const Value& value);
		void insert(Key&& key, Value&& value);

		template<typename... Args>
		void emplace(Args... args);

		bool try_insert(Key& key, const Value& value);

		std::optional<Value> get(Key& key) const;
		bool erase(Key& key);
		bool contains(Key& key);
		size_t size() const;
		bool empty() const;
		void clear();
		std::vector<Key> get_all_keys() const;
		std::vector<Value> get_all_values() const;

		// read-only to iterate
		void for_each(std::function<void(const Key&, const Value&)>) const;

		// iterate: read and write both
		void for_each_mut(std::function<void(Key&, Value&)> func);

		// execute function with read lock
		template<typename Func>
		auto with_read_lock(Func func) const;

		// execute function with read lock
		template<typename Func>
		auto with_write_lock(Func func) const;

		void reserve(size_t size);

		void rehash(size_t size);

		// swap with another SafeHashMap
		void swap(SafeHashMap& other) noexcept;
	};
}