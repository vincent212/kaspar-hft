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

namespace chutil
{

  template <int N>
  struct hash_set2
  {

    hash_set2() {
        for (auto& v : arr) {
            v.reserve(32);
        }
    }

    struct val_t
    {
      uint64_t k;
      bool used;
      val_t(uint64_t _k, bool _used)
      {
        k = _k;
        used = _used;
      }
    };

    std::array<std::vector<val_t>, N> arr;

    inline std::size_t hash(uint64_t k) const
    {
      return k % N;
    }

    void insert(uint64_t k)
    {
      auto h = hash(k);
      auto &a = arr[h];
      if (a.size() == 0)
      {
        a.push_back(val_t(k, true));
        return;
      }
      std::size_t i = 0;
      bool found_empty = false;
      for (; i < a.size(); i++)
      {
        if (!a[i].used)
        {
          found_empty = true;
          break;
        }
      }
      if (found_empty)
      {
        a[i].k = k;
        a[i].used = true;
      }
      else
      {
        arr[h].push_back(val_t(k, true));
      }
    }

    bool get(uint64_t k) const
    {
      auto h = hash(k);
      const auto &v = arr[h];
      for (const auto &val : v)
      {
        if (val.used && val.k == k)
          return true;
      }
      return false;
    }

    void del(uint64_t k)
    {
      auto h = hash(k);
      auto &v = arr[h];
      for (auto &val : v)
      {
        if (val.k == k)
        {
          val.used = false;
          return;
        }
      }
    }

    void clear()
    {

      for (std::size_t i = 0; i < arr.size(); i++)
      {
        arr[i].clear();
      }
    }



  };

}