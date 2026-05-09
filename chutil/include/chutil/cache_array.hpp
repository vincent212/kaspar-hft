#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <iostream>
#include <vector>
#include <boost/unordered/unordered_flat_map.hpp>

namespace chutil
{

  template <typename valT, int N>
  struct cache_array
  {
    boost::unordered_flat_map<uint64_t, valT> map;

    inline std::size_t hash(uint64_t k) const
    {
      return k % N;
    }

    void insert(uint64_t k, const valT &v, bool overwrite = false)
    {
      if (!overwrite)
        ASSERT(map.find(k) == map.end(), "already has val");
      map[k] = v;
    }

    bool get(uint64_t k, valT const *&v) const
    {
      auto it = map.find(k);
      if (it == map.end())
        return false;
      v = &it->second;
      return true;
    }

    void clear()
    {
      map.clear();
    }
  };

}