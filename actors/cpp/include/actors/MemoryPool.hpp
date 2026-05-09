#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <exception>
#include <boost/circular_buffer.hpp>
#include "actors/HybridCB.hpp"

//#define DISABLE_MEMORY_POOL

#define CACHE_LINE_SIZE 64

namespace actors
{

#ifndef DISABLE_MEMORY_POOL
  template <size_t AllocCacheCount, size_t FreedCacheCount>
  struct ThreadLocalCache
  {
    boost::circular_buffer<void *> allocCache{AllocCacheCount};
    boost::circular_buffer<void *> freedCache{FreedCacheCount};
    bool hasAllocated = false;

    ~ThreadLocalCache();
  };

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount>
  inline ThreadLocalCache<AllocCacheCount, FreedCacheCount> &getThreadLocalCache()
  {
    static thread_local ThreadLocalCache<AllocCacheCount, FreedCacheCount> cache;
    return cache;
  }

#endif

  template <typename T, size_t AllocCacheCount = 8, size_t FreedCacheCount = 8, size_t FreeBlocksCapacity = 1024, size_t AllocatedBlocksCapacity = (FreeBlocksCapacity / AllocCacheCount)>
  struct MemoryPool
  {
    MemoryPool() = default;

#ifndef DISABLE_MEMORY_POOL
    void *operator new(size_t size);
    void operator delete(void *ptr, size_t size);
    void *operator new[](size_t) = delete;
    void operator delete[](void *, size_t) = delete;
#else
    void *operator new(size_t size)
    {
      return ::operator new(size);
    }

    void operator delete(void *ptr, size_t)
    {
      ::operator delete(ptr);
    }

    void *operator new[](size_t) = delete;
    void operator delete[](void *, size_t) = delete;
#endif

  private:
#ifndef DISABLE_MEMORY_POOL
    static HybridCB<void *> freeBlocks;
    static HybridCB<void *> allocatedBlocks;
    alignas(64) static std::mutex mutex_;

    static void fillAllocCacheFromFreeBlocks();
    static void flushFreedCache();

  public:
    static void cleanup();
#endif
  };

#ifndef DISABLE_MEMORY_POOL
  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  HybridCB<void *> MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::freeBlocks{FreeBlocksCapacity};

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  HybridCB<void *> MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::allocatedBlocks{AllocatedBlocksCapacity};

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  alignas(64) std::mutex MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::mutex_;

  // Template implementations
  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  void *MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::operator new(size_t size)
  {
    if (size != sizeof(T)) [[unlikely]]
    {
      throw std::invalid_argument("MemoryPool can only allocate objects of type T");
    }

    ThreadLocalCache<AllocCacheCount, FreedCacheCount> &cache = getThreadLocalCache<T, AllocCacheCount, FreedCacheCount>();
    cache.hasAllocated = true;

    if (!cache.allocCache.empty())
    {
      void *ptr = cache.allocCache.back();
      cache.allocCache.pop_back();
      return ptr;
    }

    fillAllocCacheFromFreeBlocks();

    // Calculate how many blocks are needed to fill the cache
    size_t needed = AllocCacheCount - cache.allocCache.size();

    if (needed > 0)
    {
      std::lock_guard<std::mutex> lock(mutex_);

      size_t aligned_size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);

      if (aligned_size < size) [[unlikely]]
      {
        throw std::overflow_error("Size alignment overflow");
      }

      // Allocate needed + AllocCacheCount blocks total
      size_t total_blocks = needed + AllocCacheCount;
      void *blocks = std::aligned_alloc(CACHE_LINE_SIZE, aligned_size * total_blocks);
      if (!blocks)
      {
        throw std::bad_alloc();
      }

      // Track the original allocated block for cleanup
      allocatedBlocks.push_back(blocks);

      char *blockPtr = static_cast<char *>(blocks);

      // Put 'needed' blocks into thread-local cache (fills it to capacity)
      for (size_t i = 0; i < needed; ++i)
      {
        void *cacheBlock = blockPtr + i * aligned_size;
        cache.allocCache.push_back(cacheBlock);
      }

      // Put AllocCacheCount blocks into shared pool
      for (size_t i = needed; i < total_blocks; ++i)
      {
        void *freeBlock = blockPtr + i * aligned_size;
        freeBlocks.push_back(freeBlock);
      }
    }

    // Cache is now guaranteed to have blocks, pop one and return
    void *ptr = cache.allocCache.back();
    cache.allocCache.pop_back();
    return ptr;
  }

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  void MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::operator delete(void *ptr, size_t size)
  {
    if (!ptr) return;  // delete nullptr is valid
    if (size != sizeof(T))
    {
      std::terminate();  // cannot throw from noexcept operator delete
    }

    ThreadLocalCache<AllocCacheCount, FreedCacheCount> &cache = getThreadLocalCache<T, AllocCacheCount, FreedCacheCount>();

    // Only cache in thread-local storage if this thread has allocated objects
    if (cache.hasAllocated)
    {
      if (!cache.allocCache.full())
      {
        cache.allocCache.push_back(ptr);
        return;
      }
    }

    if (!cache.freedCache.full())
    {
      cache.freedCache.push_back(ptr);
      return;
    }

    // freedCache is full, flush it
    flushFreedCache();
    cache.freedCache.push_back(ptr);
  }

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  void MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::fillAllocCacheFromFreeBlocks()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ThreadLocalCache<AllocCacheCount, FreedCacheCount> &cache = getThreadLocalCache<T, AllocCacheCount, FreedCacheCount>();

    while (!freeBlocks.empty() && !cache.allocCache.full())
    {
      // Use LIFO (stack) - get hottest blocks from back for better cache locality
      void *ptr = freeBlocks.back();
      freeBlocks.pop_back();
      cache.allocCache.push_back(ptr);
    }
  }

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  void MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::flushFreedCache()
  {
    ThreadLocalCache<AllocCacheCount, FreedCacheCount> &cache = getThreadLocalCache<T, AllocCacheCount, FreedCacheCount>();
    std::lock_guard<std::mutex> lock(mutex_);

    while (!cache.freedCache.empty())
    {
      void *ptr = cache.freedCache.front();
      cache.freedCache.pop_front();
      freeBlocks.push_back(ptr);
    }
  }

  template <typename T, size_t AllocCacheCount, size_t FreedCacheCount, size_t FreeBlocksCapacity, size_t AllocatedBlocksCapacity>
  void MemoryPool<T, AllocCacheCount, FreedCacheCount, FreeBlocksCapacity, AllocatedBlocksCapacity>::cleanup()
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Free all originally allocated blocks
    for (void *block : allocatedBlocks)
    {
      std::free(block);
    }
    allocatedBlocks.clear();
    freeBlocks.clear();
  }

  template <size_t AllocCacheCount, size_t FreedCacheCount>
  inline ThreadLocalCache<AllocCacheCount, FreedCacheCount>::~ThreadLocalCache()
  {
    // Clear caches on thread termination
    // Note: We don't free individual blocks here as they are managed by the pool
    // The actual memory will be freed in MemoryPool::cleanup()
    allocCache.clear();
    freedCache.clear();
  }

#endif

}  // namespace actors
