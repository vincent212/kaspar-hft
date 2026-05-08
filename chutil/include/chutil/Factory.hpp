#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#error "Factory.hpp is deprecated and should not be included. Use standard allocators instead."

#include "chutil/Macros.hpp"
#include "boost/circular_buffer.hpp"
#include <stdlib.h>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

#define FACTORYDONOTUSE
#define NOFACTORY_SPINLOCK

namespace chutil
{
  template <typename T>
  class Factory
  {
    typedef boost::circular_buffer<T*> pool_t;
    // use static because there is only one factory per type
    static pool_t pool;

  public:

    static T* malloc() noexcept
    {
#ifdef FACTORYDONOTUSE
      ERR("Factory is disabled");
#endif
      // there should be one factory objects or one template where the lock is 
      // optionally compiled in
#ifndef NOFACTORY_SPINLOCK
      // BEWARE OF SPINLOCK AS IT CAN CAUSE LIVE LOCKS WHEN PRIO IVERSION OCCURS
      chutil::ScopedLock lock(spin);
#endif
      T* t=0;
      if (pool.size() > 0)
      {
        t = pool[0];
        pool.pop_front();
        return t;
      }
      const std::size_t chunk_sz = 64;
      std::size_t obj_sz;
      if (sizeof(T) <= chunk_sz)
      {
        obj_sz = chunk_sz;
      }
      else if (sizeof(T) % chunk_sz == 0)
      {
        obj_sz = sizeof(T);
      }
      else
      {
        auto n = sizeof(T) / chunk_sz;
        obj_sz = (n + 1) * chunk_sz;
      }
#ifdef FACTORYMEMALIGN
      int rc=posix_memalign((void**)(&t), chunk_sz, obj_sz * 8);
      ASSERT(!rc,"posix_memalign failed");
#else
      t = (T*) ::malloc(obj_sz * 8);
#endif
      for (std::size_t i = 1; i < 8; i++)
      {
        char* obj = (reinterpret_cast<char*>(t) + i * obj_sz);
        if (pool.reserve() == 0)
        {
          pool.rset_capacity(pool.capacity() + 512);
        }
        pool.push_back((T*)obj);
      }
      return t;
    }

    static void free(T* obj) noexcept
    {
#ifndef NOFACTORY_SPINLOCK
      chutil::ScopedLock lock(spin);
#endif
      if (pool.reserve() == 0)
      {
        pool.rset_capacity(pool.capacity() + 512);
      }
      pool.push_back(obj);
      return;
    }

  };

  template<typename T> typename Factory<T>::pool_t Factory<T>::pool;

}
