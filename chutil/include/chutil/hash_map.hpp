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

namespace chutil {


template<typename valT, int N>
struct hash_map {
    typedef std::unordered_map<uint64_t, valT> kv_t;

    std::array<kv_t, N> arr;

    inline std::size_t hash(uint64_t k) const {
        return k % N;
    }

    void insert(uint64_t k, const valT& v) {
        auto h = hash(k);
        arr[h][k] = v;
    }

    bool get(uint64_t k, const valT* &v) const {
        auto h = hash(k);
        const kv_t& m = arr[h]; // Fixed: Changed CA to kv_t
        auto p = m.find(k);
        if (p == m.end()) {
            v = nullptr; // Fixed: Changed 0 to nullptr for clarity
            return false;
        }
        v = &p->second;
        return true;
    }

    void del(uint64_t k) {
        auto h = hash(k);
        auto& m = arr[h];
        m.erase(k);
    }

    void clear() {
        for (auto& bucket : arr) { // Optimized: Use range-based for loop
            bucket.clear();
        }
    }

    std::vector<valT> values() const {
        std::vector<valT> result;
        for (const auto& bucket : arr) {
            for (const auto& pair : bucket) {
                result.push_back(pair.second);
            }
        }
        return result;
    }

    struct iterator {
        using outer_iter_t = typename std::array<kv_t, N>::const_iterator;
        using inner_iter_t = typename kv_t::const_iterator;

        outer_iter_t outer_it, outer_end;
        inner_iter_t inner_it;

        iterator(outer_iter_t o_it, outer_iter_t o_end)
            : outer_it(o_it), outer_end(o_end) {
            if (outer_it != outer_end) {
                inner_it = outer_it->begin();
                advance_to_valid();
            }
        }

        void advance_to_valid() {
            while (outer_it != outer_end && inner_it == outer_it->end()) {
                ++outer_it;
                if (outer_it != outer_end)
                    inner_it = outer_it->begin();
            }
        }

        iterator& operator++() {
            ++inner_it;
            advance_to_valid();
            return *this;
        }

        const std::pair<const uint64_t, valT>& operator*() const {
            return *inner_it;
        }

        const std::pair<const uint64_t, valT>* operator->() const {
            return &(*inner_it);
        }

        bool operator==(const iterator& other) const {
            return outer_it == other.outer_it && (outer_it == outer_end || inner_it == other.inner_it);
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
    };

    iterator begin() const {
        return iterator(arr.begin(), arr.end());
    }

    iterator end() const {
        return iterator(arr.end(), arr.end());
    }


};


}
