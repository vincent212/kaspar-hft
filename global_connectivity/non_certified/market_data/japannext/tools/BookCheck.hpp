#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Driving real OB instances synchronously from the handler (as
// unit_test/src/test_mbp2mbo_ob.cpp does), and comparing books:
//  - ObSink: a Sink with one frame::ob::act::OB per instrument, created on the
//    instrument's first order, fed with fast_send;
//  - compare_ob: OB's top levels ("bbbo", "qat") against the handler's book;
//  - compare_handlers: two handlers' books, order by order.

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "frame/ob/act/OB.hpp"
#include "jnx_md/Handler.hpp"
#include "jnx_md/Types.hpp"

namespace jnx_md::check
{
  class ObSink : public Sink
  {
  public:
    explicit ObSink(bool cross_check = true) : cross_check_(cross_check) {}
    ~ObSink() override;

    void book(int32_t asset, const bfile::l3_t &r) override;
    void all_books(const bfile::l3_t &r) override;
    void recorder(const bfile::l3_t &r) override;

    frame::ob::act::OB *ob(int32_t asset) const
    {
      auto it = obs_.find(asset);
      return it == obs_.end() ? nullptr : it->second.get();
    }
    const std::map<int32_t, std::unique_ptr<frame::ob::act::OB>> &obs() const { return obs_; }
    std::set<int32_t> take_touched()
    {
      std::set<int32_t> t;
      t.swap(touched_);
      return t;
    }
    // Shutdown every OB; returns the sum of the "badpx" counts they print.
    uint64_t shutdown_bad_prices();

    uint64_t records = 0, recorder_records = 0, all_book_records = 0;

  private:
    bool cross_check_;
    std::map<int32_t, std::unique_ptr<frame::ob::act::OB>> obs_;
    std::map<int32_t, bfile::l3_t> pending_status_;
    std::set<int32_t> touched_;
    void send(frame::ob::act::OB *ob, const bfile::l3_t &r);
    frame::ob::act::OB *create(int32_t asset);
  };

  struct Level
  {
    int64_t px = 0;   // Kaspar ticks
    long qty = 0, count = 0;
    bool operator==(const Level &o) const = default;
  };

  // OB's best bid and ask in ticks.
  std::pair<int, int> bbbo(frame::ob::act::OB &ob);
  // OB's size and order count at a price (ticks).
  Level qat(frame::ob::act::OB &ob, bool bid, int64_t px);
  // OB's top n non-empty levels on a side, found by stepping tick by tick from
  // its best price, at most max_ticks ticks.
  std::vector<Level> ob_levels(frame::ob::act::OB &ob, bool bid, int maxpx, size_t n,
                               int max_ticks);
  // The handler's top n levels on a side, in ticks.
  std::vector<Level> handler_levels(const Instrument &i, bool bid, size_t n);

  struct ObCompare
  {
    uint64_t instruments = 0, levels = 0, mismatched_instruments = 0, mismatched_levels = 0,
             bbo_mismatches = 0;
    std::vector<std::string> examples;
  };
  // Compare the top n levels per side of OB and the handler for one instrument.
  void compare_ob(const Instrument &i, frame::ob::act::OB &ob, size_t n, int max_ticks,
                  ObCompare &out);

  struct HandlerCompare
  {
    uint64_t instruments = 0, orders = 0, mismatches = 0;
    std::vector<std::string> examples;
  };
  // Every book of a and b: the same levels, and in each the same orders (by
  // current Order Number), sizes and queue order. orderIDs are not compared:
  // after a snapshot a replaced order carries its new number as orderID.
  HandlerCompare compare_handlers(const Handler &a, const Handler &b);
}
