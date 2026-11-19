/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// BookCheck: rebuilds one instrument's book from the records the handler
// sends to OB, and compares it with the book LMAX sent. Used by the tests and
// by the live check (tools/lmax_md_check.cpp). NOT CERTIFIED.
//
// It applies l3_mbo_v2_t adds, changes and deletes by orderID, as OB does,
// clearing the book when a run of recovery records starts and on a book-clear
// record (l3_chr_v2_t, sent to one instrument's book), and counts chunks
// per level. compare() checks that every one of the best `depth` levels LMAX
// sent has ceil(size / chunk) chunks at its price and that nothing else is in
// the rebuilt book; crossed() checks the rebuilt book's top.

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bfile/r_l3.hpp"

namespace lmax::md
{
  class BookCheck
  {
  public:
    BookCheck(int32_t sec_id, double tick, double chunk) : sec_id_(sec_id), tick_(tick), chunk_(chunk) {}

    void on_record(const bfile::l3_t &r);

    // Mismatches between the rebuilt book and LMAX's levels (best first);
    // each is described in `why` (first few only).
    size_t compare(const std::vector<std::pair<double, double>> &bids,
                   const std::vector<std::pair<double, double>> &asks, size_t depth, std::string &why) const;

    bool crossed() const;
    int64_t best(bool bid) const; // ticks; 0 if empty
    size_t orders() const { return orders_.size(); }
    uint64_t executions() const { return execs_; }
    uint64_t errors() const { return errors_; } // delete/change of an unknown order, duplicate add

  private:
    struct Ord
    {
      bool bid;
      int64_t ticks;
      uint32_t qty;
    };
    int32_t sec_id_;
    double tick_, chunk_;
    std::unordered_map<uint64_t, Ord> orders_;
    std::map<int64_t, uint64_t> bids_, asks_; // ticks -> chunk count
    bool in_recovery_ = false;
    uint64_t execs_ = 0, errors_ = 0;

    void level(bool bid, int64_t t, int64_t d);
  };
}
