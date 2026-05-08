#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <array>
#include <sstream>
#include <string>
#include <cmath>
#include <optional>
// immintrin.h is x86-only, conditionally include
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif
#include "chutil/places.hpp"
#include "enum/e_names.hpp"

#define NLEVELS 16

namespace frame
{
  namespace mda
  {
    namespace msg
    {
      struct point
      {

        point() = default;

        bool operator==(const point& other) const noexcept {
          bool result = true;

#define DEBUG_POINT_COMPARE

#ifdef DEBUG_POINT_COMPARE
          if (baddata != other.baddata) {
            std::cerr << "point baddata differs: " << baddata << " vs " << other.baddata << std::endl;
            result = false;
          }
          if (sym != other.sym) {
            std::cerr << "point sym differs: " << sym << " vs " << other.sym << std::endl;
            result = false;
          }
          if (side != other.side) {
            std::cerr << "point side differs: " << static_cast<int>(side) << " vs " << static_cast<int>(other.side) << std::endl;
            result = false;
          }
          // if (action != other.action) {
          //   std::cerr << "point action differs: " << static_cast<int>(action) << " vs " << static_cast<int>(other.action) << std::endl;
          //   result = false;
          // }
          if (mev != other.mev) {
            std::cerr << "point mev differs: " << static_cast<int>(mev) << " vs " << static_cast<int>(other.mev) << std::endl;
            result = false;
          }
          if (bid_px != other.bid_px) {
            std::cerr << "point bid_px differs" << std::endl;
            result = false;
          }
          if (ask_px != other.ask_px) {
            std::cerr << "point ask_px differs" << std::endl;
            result = false;
          }
          if (bid_sz != other.bid_sz) {
            std::cerr << "point bid_sz differs" << std::endl;
            result = false;
          }
          if (ask_sz != other.ask_sz) {
            std::cerr << "point ask_sz differs" << std::endl;
            result = false;
          }
          return result;
#else
          return baddata == other.baddata &&
           sym == other.sym &&
           side == other.side &&
           action == other.action &&
           mev == other.mev &&
           bid_px == other.bid_px &&
           ask_px == other.ask_px &&
           bid_sz == other.bid_sz &&
           ask_sz == other.ask_sz;
#endif
        }

        // Fast comparison for bid_px, ask_px, bid_sz, ask_sz only
        inline bool fast_compare(const point& other) const noexcept {
          static_assert(NLEVELS == 16, "fast_compare assumes NLEVELS == 16");
#if defined(__x86_64__) || defined(_M_X64)
          // AVX2 optimized path for x86-64
          // Load and compare bid_px arrays (16 ints = 512 bits = 2 AVX2 registers)
          const __m256i bid_px1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bid_px.data()));
          const __m256i bid_px2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bid_px.data() + 8));
          const __m256i other_bid_px1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.bid_px.data()));
          const __m256i other_bid_px2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.bid_px.data() + 8));

          const __m256i bid_px_cmp1 = _mm256_cmpeq_epi32(bid_px1, other_bid_px1);
          const __m256i bid_px_cmp2 = _mm256_cmpeq_epi32(bid_px2, other_bid_px2);

          // Load and compare ask_px arrays
          const __m256i ask_px1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ask_px.data()));
          const __m256i ask_px2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ask_px.data() + 8));
          const __m256i other_ask_px1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.ask_px.data()));
          const __m256i other_ask_px2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.ask_px.data() + 8));

          const __m256i ask_px_cmp1 = _mm256_cmpeq_epi32(ask_px1, other_ask_px1);
          const __m256i ask_px_cmp2 = _mm256_cmpeq_epi32(ask_px2, other_ask_px2);

          // Load and compare bid_sz arrays
          const __m256i bid_sz1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bid_sz.data()));
          const __m256i bid_sz2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bid_sz.data() + 8));
          const __m256i other_bid_sz1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.bid_sz.data()));
          const __m256i other_bid_sz2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.bid_sz.data() + 8));

          const __m256i bid_sz_cmp1 = _mm256_cmpeq_epi32(bid_sz1, other_bid_sz1);
          const __m256i bid_sz_cmp2 = _mm256_cmpeq_epi32(bid_sz2, other_bid_sz2);

          // Load and compare ask_sz arrays
          const __m256i ask_sz1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ask_sz.data()));
          const __m256i ask_sz2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ask_sz.data() + 8));
          const __m256i other_ask_sz1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.ask_sz.data()));
          const __m256i other_ask_sz2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(other.ask_sz.data() + 8));

          const __m256i ask_sz_cmp1 = _mm256_cmpeq_epi32(ask_sz1, other_ask_sz1);
          const __m256i ask_sz_cmp2 = _mm256_cmpeq_epi32(ask_sz2, other_ask_sz2);

          // Combine all comparison results with AND operations
          const __m256i combined1 = _mm256_and_si256(
              _mm256_and_si256(bid_px_cmp1, ask_px_cmp1),
              _mm256_and_si256(bid_sz_cmp1, ask_sz_cmp1));
          const __m256i combined2 = _mm256_and_si256(
              _mm256_and_si256(bid_px_cmp2, ask_px_cmp2),
              _mm256_and_si256(bid_sz_cmp2, ask_sz_cmp2));

          // Check if all bits are set (all comparisons true)
          const int mask1 = _mm256_movemask_epi8(combined1);
          const int mask2 = _mm256_movemask_epi8(combined2);

          // All bits should be 1 for a match (0xFFFFFFFF for each 256-bit register)
          return (mask1 == -1) && (mask2 == -1);
#else
          // Fallback for non-x86 (e.g., ARM)
          return bid_px == other.bid_px &&
                 ask_px == other.ask_px &&
                 bid_sz == other.bid_sz &&
                 ask_sz == other.ask_sz;
#endif
        }

        // todo: get rid of these they are in payload
        bool baddata;
        int sym;
        en::bs side;
        en::mt action;
        en::md mev;

        using book_arry_t = std::array<int, NLEVELS>;

        book_arry_t
            bid_px,
            ask_px,
            bid_sz,
            ask_sz;

        // std::optional<
        //     std::tuple<double, double, double, double>>
        // imp_mid(int decay,
        //         std::size_t lev = NLEVELS) noexcept;

        // returns mid,bo,bb,ba
        std::optional<std::tuple<double, double, int, int>> 
        non_zero_mid_bo() const noexcept;

        // w bid ask
        std::optional<double> w_bid(int sz) const noexcept;
        std::optional<double> w_ask(int sz) const noexcept;

        // the x% w bid/ask
        std::optional<std::tuple<double, double, double>> w_bid_ask(double pcnt) const noexcept;

        // Optimized version - single pass through book
        std::optional<std::tuple<double, double, double>> w_bid_ask_fast(double pcnt) const noexcept;

        // uncross the book
        void uncross() noexcept;
        void fill_best(int, int) noexcept;
        int nz_bid() const noexcept;
        int nz_ask() const noexcept;
        bool zero_book() const noexcept
        {
          return nz_bid() == NLEVELS || nz_ask() == NLEVELS;
        }

        auto nz_bid_val() const noexcept
        {
          std::optional<std::tuple<int, int>> ret;
          auto idx = nz_bid();
          if (idx == NLEVELS)
            return ret;
          else
          {
            ret = std::make_tuple(bid_px[idx], bid_sz[idx]);
            return ret;
          }
        }

        auto nz_ask_val() const noexcept
        {
          std::optional<std::tuple<int, int>> ret;
          auto idx = nz_ask();
          if (idx == NLEVELS)
            return ret;
          else
          {
            ret = std::make_tuple(ask_px[idx], ask_sz[idx]);  
            return ret;
          }
        }

        auto bsz_at_px(int px) const noexcept
        {
          std::optional<int> ret;
          for (std::size_t i = 0; i < NLEVELS; ++i)
          {
            if (bid_px[i] == px)
            {
              ret = bid_sz[i];
              break;
            }
          }
          return ret;
        }

        auto asz_at_px(int px) const noexcept
        {
          std::optional<int> ret;
          for (std::size_t i = 0; i < NLEVELS; ++i)
          {
            if (ask_px[i] == px)
            {
              ret = ask_sz[i];
              break;
            }
          }
          return ret;
        }
        
        void shift_zero_bid() noexcept;
        void shift_zero_ask() noexcept;
        void shift_zero() noexcept { shift_zero_bid(); shift_zero_ask(); }
      
      friend std::ostream& operator<<(std::ostream& os, const point& p) {
        auto print_array = [&os](const char* name, const book_arry_t& arr) {
          os << "\"" << name << "\":[";
          for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i) os << ',';
            os << arr[i];
          }
          os << "]";
        };

        os << "{";
        os << "\"baddata\":" << (p.baddata ? "true" : "false") << ",";
        os << "\"sym\":" << p.sym << ",";
        os << "\"side\":" << static_cast<int>(p.side) << ",";
        os << "\"action\":" << static_cast<int>(p.action) << ",";
        os << "\"mev\":" << static_cast<int>(p.mev) << ",";
        print_array("bid_px", p.bid_px); os << ",";
        print_array("ask_px", p.ask_px); os << ",";
        print_array("bid_sz", p.bid_sz); os << ",";
        print_array("ask_sz", p.ask_sz);
        os << "}";
        return os;
      }
      
      };
    }
  }
}
