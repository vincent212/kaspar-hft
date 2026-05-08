#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#ifdef USEPOOLED
#include "Factory.hpp"
#endif
#include <ostream>

namespace chutil
{

  class NoBase
  {
  };

  template <typename T, typename B = NoBase>
  class PooledObject : public B
  {

#ifdef USEPOOLED
    typedef Factory<T> factory_t;
#endif

  public:

#ifdef USEPOOLED
    static void* operator new (std::size_t) noexcept
    {
      void* p = factory_t::malloc(); // we know the size
      return p;
    } // A's default ctor implicitly called here

    static void operator delete (void* p) noexcept
    {
      factory_t::free(static_cast<T*>(p));
    } // A's dtor implicitly called at this point

    friend std::ostream& operator<<(std::ostream& out, const PooledObject&)
    {
      out << "stream operator not overriden";
      return out;
    }

#endif

  };

}
