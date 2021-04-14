#pragma once

//#include <iostream>
#include <memory>
//#include <stdexcept>
#include <type_traits>

namespace caching {

// See https://stackoverflow.com/a/24289614
template <typename T, bool UseFreeList = true, unsigned BlockSize = 1024>
class pool_allocator {
  static_assert(BlockSize != 0, "The BlockSize must not be 0");
  struct Block {
    union DataField {
      DataField *nextFree;
      T data;
    };

    using value_type =
        std::aligned_storage_t<sizeof(DataField), alignof(DataField)>;
    Block *next;
    value_type data[0];

    static Block *create(Block *nxt, unsigned n) {
      // sizeof(Block) already contains the padding between Block::next and
      // Block::data
      auto ret = reinterpret_cast<Block *>(::new (std::align_val_t{
          alignof(Block)}) uint8_t[sizeof(Block) + (n * sizeof(value_type))]);
      ret->next = nxt;
      return ret;
    }
  };

  template <bool FL> struct MemoryPool;
  template <> struct MemoryPool<true> {
    Block *pool;
    typename Block::DataField *freeList;

    MemoryPool() noexcept : pool(nullptr), freeList(nullptr) {}
    MemoryPool(std::nullptr_t) noexcept : MemoryPool() {}
  };
  template <> struct MemoryPool<false> {
    Block *pool;
    MemoryPool() noexcept : pool(nullptr) {}
    MemoryPool(std::nullptr_t) noexcept : MemoryPool() {}
  };

  MemoryPool<UseFreeList> mpool;

  unsigned currBlockSize;
  unsigned index;

public:
  pool_allocator(unsigned reserved = BlockSize) noexcept
      : mpool(), index(reserved),
        currBlockSize(reserved){
            // std::cout << "> Ctor(reserved=" << reserved << ")\n";
        };

  pool_allocator(const pool_allocator &other) noexcept
      : pool_allocator(other.currBlockSize) {}
  template <typename U, bool FL>
  pool_allocator(const pool_allocator<U, FL> &other) noexcept
      : pool_allocator(other.minCapacity()) {}

  pool_allocator(pool_allocator &&other) noexcept
      : mpool(other.mpool), index(other.index),
        currBlockSize(other.currBlockSize) {
    other.mpool = nullptr;
  }

  pool_allocator &operator=(const pool_allocator &other) noexcept = delete;
  pool_allocator &operator=(pool_allocator &&other) noexcept {
    this->pool_allocator::~pool_allocator();
    ::new (this) pool_allocator(std::move(other));
    return *this;
  }

  ~pool_allocator() {
    // The data inside the blocks is assumed to be already destroyed.
    for (auto p = mpool.pool; p;) {
      auto nxt = p->next;

      ::operator delete[](reinterpret_cast<uint8_t *>(p),
                          std::align_val_t{alignof(Block)});
      //::delete[] reinterpret_cast<uint8_t *>(p);
      p = nxt;
    }

    mpool = nullptr;
  }

  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;

  template <class U> struct rebind {
    using other = pool_allocator<U, UseFreeList, BlockSize>;
  };

  pointer allocate(size_t n) {
    if (n != 1) {
      // cannot allocate arrays, since the Blocks are not contiguous
      // in memory. So, fallback to the default allocator
      return reinterpret_cast<pointer>(
          ::new std::aligned_storage_t<sizeof(T), alignof(T)>[n]);
    }
    if constexpr (UseFreeList) {
      if (mpool.freeList) {
        auto fld = mpool.freeList;
        mpool.freeList = fld->nextFree;
        return reinterpret_cast<pointer>(fld);
      }
    }

    if (index == currBlockSize) {

      Block *nwPl;
      if (mpool.pool) {
        // std::cout << "> Allocate " << BlockSize << " elements" << std::endl;
        nwPl = Block::create(mpool.pool, BlockSize);

        if (currBlockSize != BlockSize)
          currBlockSize = BlockSize;
      } else {
        // std::cout << "> Allocate " << currBlockSize << " elements" <<
        // std::endl;
        nwPl = Block::create(mpool.pool, currBlockSize);
      }
      mpool.pool = nwPl;
      index = 1;
      return reinterpret_cast<pointer>(&nwPl->data[0]);
    }

    return reinterpret_cast<pointer>(&mpool.pool->data[index++]);
  }

  void deallocate(pointer ptr, size_t n) {
    if (n != 1) {
      ::delete[] reinterpret_cast<
          std::aligned_storage_t<sizeof(T), alignof(T)> *>(ptr);
      return;
    }
    if constexpr (UseFreeList) {
      // Only insert the pointer into the free-list. Actual deallocation happens
      // in the destructor of this allocator.
      auto *fl = reinterpret_cast<typename Block::DataField *>(ptr);
      fl->nextFree = mpool.freeList;
      mpool.freeList = fl;
    }
  }

  template <typename... Args> void construct(pointer ptr, Args &&... args) {
    ::new (ptr) T(std::forward<Args>(args)...);
  }
  void destroy(pointer ptr) noexcept(std::is_nothrow_destructible_v<T>) {
    ptr->T::~T();
  }

  bool operator==(const pool_allocator &other) const noexcept { return true; }
  bool operator!=(const pool_allocator &other) const noexcept {
    return !(*this == other);
  }

  // For internal use only
  unsigned minCapacity() const noexcept { return currBlockSize; }
};
} // namespace caching