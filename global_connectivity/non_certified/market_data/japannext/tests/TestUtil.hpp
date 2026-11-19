#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Test helpers: a sink that keeps every record, and ITCH messages built from
// the generated structs (values as the JNX ITCH 2.02 tables define them).

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jnx_md/Handler.hpp"
#include "jnx_md/Writer.hpp"
#include "jnx_md/gen/itch.hpp"
#include "jnx_md/gen/moldudp64.hpp"

namespace jnx_md::test
{
  namespace I = jnx::itch::from_jnx;

  struct Rec
  {
    enum Where { Book, All, Recorder } where;
    int32_t asset;
    bfile::l3_t r;
  };

  struct RecSink : Sink
  {
    std::vector<Rec> recs;
    void book(int32_t a, const bfile::l3_t &r) override { recs.push_back({Rec::Book, a, r}); }
    void all_books(const bfile::l3_t &r) override { recs.push_back({Rec::All, -1, r}); }
    void recorder(const bfile::l3_t &r) override { recs.push_back({Rec::Recorder, -1, r}); }
    // Records of type T sent to books, in order.
    template <class T>
    std::vector<T> of() const
    {
      std::vector<T> out;
      for (const auto &x : recs)
        if (x.where != Rec::Recorder)
          if (const T *p = std::get_if<T>(&x.r))
            out.push_back(*p);
      return out;
    }
    void clear() { recs.clear(); }
  };

  inline std::string T(uint32_t s)
  {
    I::TimestampSecondsMessage m{};
    m.TimestampSeconds = s;
    return bytes_of(m);
  }
  inline std::string S(uint32_t ns, std::string_view group, char ev)
  {
    I::SystemEventMessage m{};
    m.TimestampNanoseconds = ns;
    m.set_Group(group);
    m.SystemEvent = ev;
    return bytes_of(m);
  }
  inline std::string L(uint32_t table, uint32_t tick, uint32_t start)
  {
    I::PriceTickSizeMessage m{};
    m.PriceTickSizeTableId = table;
    m.PriceTickSize = tick;
    m.PriceStart = start;
    return bytes_of(m);
  }
  inline std::string R(std::string_view sicc, std::string_view group, uint32_t table,
                       uint32_t upper, uint32_t lower, uint32_t lot = 100)
  {
    I::OrderbookDirectoryMessage m{};
    m.set_OrderbookId(sicc);
    m.set_OrderbookCode("JP0000000000");
    m.set_Group(group);
    m.RoundLotSize = lot;
    m.PriceTickSizeTableId = table;
    m.PriceDecimals = 1;
    m.UpperPriceLimit = upper;
    m.LowerPriceLimit = lower;
    return bytes_of(m);
  }
  inline std::string H(uint32_t ns, std::string_view sicc, std::string_view group, char st)
  {
    I::TradingStateMessage m{};
    m.TimestampNanoseconds = ns;
    m.set_OrderbookId(sicc);
    m.set_Group(group);
    m.TradingState = st;
    return bytes_of(m);
  }
  inline std::string Y(uint32_t ns, std::string_view sicc, std::string_view group, char st)
  {
    I::ShortSellingPriceRestrictionStateMessage m{};
    m.TimestampNanoseconds = ns;
    m.set_OrderbookId(sicc);
    m.set_Group(group);
    m.ShortSellingState = st;
    return bytes_of(m);
  }
  inline std::string A(uint32_t ns, uint64_t num, char side, uint32_t qty, std::string_view sicc,
                       std::string_view group, uint32_t px)
  {
    I::OrderAddedMessage m{};
    m.TimestampNanoseconds = ns;
    m.OrderNumber = num;
    m.BuySellIndicator = side;
    m.Quantity = qty;
    m.set_OrderbookId(sicc);
    m.set_Group(group);
    m.Price = px;
    return bytes_of(m);
  }
  inline std::string F(uint32_t ns, uint64_t num, char side, uint32_t qty, std::string_view sicc,
                       std::string_view group, uint32_t px, char type = 'Q')
  {
    I::OrderAddedWithAttributesMessage m{};
    m.TimestampNanoseconds = ns;
    m.OrderNumber = num;
    m.BuySellIndicator = side;
    m.Quantity = qty;
    m.set_OrderbookId(sicc);
    m.set_Group(group);
    m.Price = px;
    m.set_Attribution("");
    m.OrderType = type;
    return bytes_of(m);
  }
  inline std::string E(uint32_t ns, uint64_t num, uint32_t qty, uint64_t match = 1)
  {
    I::OrderExecutedMessage m{};
    m.TimestampNanoseconds = ns;
    m.OrderNumber = num;
    m.ExecutedQuantity = qty;
    m.MatchNumber = match;
    return bytes_of(m);
  }
  inline std::string D(uint32_t ns, uint64_t num)
  {
    I::OrderDeletedMessage m{};
    m.TimestampNanoseconds = ns;
    m.OrderNumber = num;
    return bytes_of(m);
  }
  inline std::string U(uint32_t ns, uint64_t orig, uint64_t num, uint32_t qty, uint32_t px)
  {
    I::OrderReplacedMessage m{};
    m.TimestampNanoseconds = ns;
    m.OriginalOrderNumber = orig;
    m.NewOrderNumber = num;
    m.Quantity = qty;
    m.Price = px;
    return bytes_of(m);
  }

  constexpr const char *kSession = "1697659284";
  constexpr uint64_t kMidnight = 1697641200000000000ull;   // 2023-10-19 00:00 JST

  // Feeds packets to a handler, numbering them.
  struct Feeder
  {
    Handler &h;
    uint64_t seq = 1;
    uint64_t now = kMidnight;
    std::string session = kSession;
    std::string make(const std::vector<std::string> &msgs)
    {
      const std::string p = mold_packet(session, seq, msgs);
      seq += msgs.size();
      return p;
    }
    void send(const std::string &p)
    {
      now += 1000;
      h.on_packet(p.data(), p.size(), now);
    }
    void operator()(const std::vector<std::string> &msgs) { send(make(msgs)); }
  };

  // The tick table 37 rows of the sample (feed.md section 8).
  inline std::vector<std::string> table37()
  {
    return {L(37, 1, 0), L(37, 5, 30000), L(37, 10, 50000), L(37, 50, 300000),
            L(37, 100, 500000), L(37, 500, 3000000), L(37, 1000, 5000000)};
  }

  inline Handler::Config config(bool assets = false, unsigned stream = 0,
                                Handler::Join join = Handler::Join::FromOne)
  {
    Handler::Config c;
    c.join = join;
    c.dispatch.midnight_ns = kMidnight;
    c.dispatch.stream = stream;
    c.instruments.register_assets = assets;
    return c;
  }
}
