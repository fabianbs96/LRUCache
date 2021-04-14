#pragma once

#include <cassert>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>

#include "caching/pool_allocator.hpp"

namespace caching {

///
/// \brief A simple LRU cache with a fixed dynamic limit. This cache is not
/// thread-safe.
///
/// \tparam TKey The key type used for fast element access
/// \tparam TValue The type of cached values
/// \tparam AllocBlockSize The number of elements to allocate at once to reduce
/// the number of total allocations
template <typename TKey, typename TValue, unsigned AllocBlockSize = 1024>
class lru_cache {
  // The cache actually does not deallocate any memory before destructing it, so
  // the allocators do not need a freelist

  using ListElemTy = std::pair<const TKey *, TValue>;
  using ListAllocTy = pool_allocator<ListElemTy, false, AllocBlockSize>;
  using ListTy = std::list<ListElemTy, ListAllocTy>;

  using MapPairTy = std::pair<const TKey, typename ListTy::iterator>;
  using MapAllocTy = pool_allocator<MapPairTy, false, AllocBlockSize>;
  using MapTy =
      std::unordered_map<TKey, typename ListTy::iterator, std::hash<TKey>,
                         std::equal_to<TKey>, MapAllocTy>;

  MapTy dict;
  mutable ListTy cache;

  size_t limit;

public:
  /// \brief Initializes a new, empty lru_cache
  /// \param limit The maximum number of elements that can be cached at a time
  explicit lru_cache(size_t limit) noexcept
      : limit(limit),
        dict(MapAllocTy((unsigned)std::min<size_t>(limit, AllocBlockSize))),
        cache(ListAllocTy((unsigned)std::min<size_t>(limit, AllocBlockSize))) {
    assert(limit && "The cache-limit may not be 0");
  }

  /// \brief Initializes a new, empty lru_cache and preallocates buffers for
  /// holding at least initCap elements
  /// \param limit The maximum number of elements that can be cached at a time
  /// \param initCap The number of elements for those memory should be
  /// preallocated
  explicit lru_cache(size_t limit, unsigned initCap)
      : limit(limit), dict(MapAllocTy(initCap)), cache(ListAllocTy(initCap)) {
    assert(limit && "The cache-limit may not be 0");
    dict.reserve(initCap);
  }

  /// \brief This is a move-only type
  lru_cache(const lru_cache &) = delete;

  /// \brief Inserts the (key, value) pair into the cache, if there is no
  /// other entry with an equivalent key or if update is true.
  ///
  /// If the limit is reached, the least recently used entry is removed
  /// before inserting. No entry is removed, if the insertion does not take
  /// place.
  /// \param key The key to insert
  /// \param value The to key associated value to insert
  /// \param update True, iff an existing key-value pair with an equivalent key
  /// should be replaced by the new key-value pair. The default value is false
  /// \return A tuple, where the first element is a pointer to the cached value
  /// and the second element denotes whether the insertion actually took place
  template <typename K, typename V>
  std::pair<TValue *, bool> insert(K &&key, V &&value, bool update = false) {

    // Is key already contained?
    auto it = dict.find(key);
    if (it != dict.end()) {
      auto lstIt = it->second;
      cache.splice(cache.end(), cache, lstIt);

      auto *retptr = &lstIt->second;
      if (update) {
        *retptr = std::forward<V>(value);
      }

      return {retptr, false};
    }

    // Key is not contained.
    // Can we just append?

    if (dict.size() != limit) {
      auto pos = cache.insert(cache.end(), {nullptr, std::forward<V>(value)});

      auto [mapIt, unused] = dict.try_emplace(std::forward<K>(key), pos);

      pos->first = &mapIt->first;
      return {&pos->second, true};
    }
    // We cannot just append, because we have reached the limit. So, delete
    // the LRU item (cache.front()) but reuse the allocated nodes for
    // the new item to insert.

    assert(!cache.empty());

    auto &front = cache.front();

    auto nod = dict.extract(*cache.front().first);

    assert(!nod.empty());

    nod.key() = std::forward<K>(key);
    nod.mapped() = cache.begin();

    front.first = &nod.key();
    front.second = std::forward<V>(value);

    auto ret = dict.insert(std::move(nod));
    assert(ret.inserted);

    cache.splice(cache.end(), cache, cache.begin());

    return {&front.second, true};
  }

  /// \brief Inserts the (key, value) pair into the cache, if there is no
  /// other entry with an equivalent key
  /// \param key The key to insert
  /// \param value The to key associated value to insert
  /// \return A mutable reference to the cached value
  template <typename K, typename V> TValue &getOrInsert(K &&key, V &&value) {
    auto [ret, unused] =
        insert(std::forward<K>(key), std::forward<V>(value), false);
    return *ret;
  }

  /// \brief Looks up the value associated to key in the cache. Updates the LRU
  /// order.
  /// \param key The key to search for
  /// \return A const reference to the cached value associated with key if
  /// found. Returns std::nullopt, iff key is not present in the cache (any
  /// more).
  std::optional<std::reference_wrapper<const TValue>>
  get(const TKey &key) const noexcept {
    auto it = dict.find(key);
    if (it != dict.end()) {
      cache.splice(cache.end(), cache, it->second);
      return std::cref(it->second->second);
    }

    return std::nullopt;
  }

  /// \brief Looks up the value associated to key in the cache. Updates the LRU
  /// order.
  /// \param key The key to search for
  /// \return A mutable reference to the cached value associated with key if
  /// found. Returns std::nullopt, iff key is not present in the cache (any
  /// more).
  std::optional<std::reference_wrapper<TValue>> get(const TKey &key) noexcept {
    auto it = dict.find(key);
    if (it != dict.end()) {
      cache.splice(cache.end(), cache, it->second);
      return std::ref(it->second->second);
    }

    return std::nullopt;
  }

  /// \brief Same as get(const TKey&)const, but without updating the LRU order.
  std::optional<std::reference_wrapper<const TValue>>
  peek(const TKey &key) const noexcept {
    auto it = dict.find(key);
    if (it != dict.end())
      return std::cref(it->second->second);

    return std::nullopt;
  }

  /// \brief Same as get(const TKey&), but without updating the LRU order.
  std::optional<std::reference_wrapper<TValue>> peek(const TKey &key) noexcept {
    auto it = dict.find(key);
    if (it != dict.end())
      return std::ref(it->second->second);

    return std::nullopt;
  }

  /// \brief Iterates all entries in the cache in LRU order (least recently used
  /// first) and calls fn(key, value) for each entry. Does not update the
  /// LRU order.
  /// \param fn The callback to invoke for each cached key-value pair. fn has
  /// two arguments: The key and the value (in this order).
  template <typename Fn>
  void forEach(Fn &&fn) const
      noexcept(noexcept(fn(std::declval<TKey>(), std::declval<TValue>()))) {
    for (auto &it : cache) {
      fn(*it.first, it.second);
    }
  }

  /// \brief Iterates all entries in the cache in LRU order (least recently used
  /// first) and calls fn(key, value) for each entry. Does not update the
  /// LRU order.
  /// \param fn The callback to invoke for each cached key-value pair. fn has
  /// two arguments: The key and the value (in this order).
  template <typename Fn>
  void forEach(Fn &&fn) noexcept(noexcept(fn(std::declval<TKey>(),
                                             std::declval<TValue>()))) {
    for (auto &it : cache) {
      fn(*it.first, it.second);
    }
  }
};
} // namespace caching