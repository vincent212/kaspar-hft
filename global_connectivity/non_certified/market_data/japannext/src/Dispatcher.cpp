/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/Dispatcher.hpp"

#include <algorithm>
#include <iostream>
#include <type_traits>

namespace jnx_md
{
  namespace
  {
    constexpr uint64_t kNumberMask = (1ull << 58) - 1;

    template <size_t N>
    std::string text(const char (&f)[N])
    {
      return std::string(jnx::get_str(f));
    }
  }

  void Dispatcher::on_itch(const jnx::itch::from_jnx::message_t &m, uint64_t seq)
  {
    seq_ = seq;
    std::visit([this](const auto &x) {
      ++st_.by_type[std::decay_t<decltype(x)>::type];
      handle(x);
    }, m);
  }

  void Dispatcher::on_glimpse(const jnx::glimpse::from_jnx::message_t &m)
  {
    seq_ = 0;
    std::visit([this](const auto &x) {
      using M = std::decay_t<decltype(x)>;
      ++st_.snapshot_by_type[M::type];
      if constexpr (M::type != 'G')
        handle(x);
    }, m);
  }

  // ITCH and GLIMPSE messages with the same type code have the same layout and
  // meaning (feed.md sections 4 and 7), so one template serves both.
  template <class M>
  void Dispatcher::handle(const M &m)
  {
    constexpr char T = M::type;
    if constexpr (T == 'T')
    {
      seconds(m.TimestampSeconds);
      if (!snapshot_)
        clock_phases(time_of(0));
    }
    else if constexpr (T == 'S')
    {
      const uint64_t t = stamp(m.TimestampNanoseconds);
      if (m.SystemEvent != 'Q' && m.SystemEvent != 'M')
        return;   // O, S, E, C: no record (section 7)
      const Phase p = m.SystemEvent == 'Q' ? Phase::Open : Phase::Closed;
      const std::string g = text(m.Group);
      std::vector<std::string> groups;
      if (!g.empty())
        groups.push_back(g);
      else
        for (const auto &[name, books] : inst_.groups())
          groups.push_back(name);
      for (const auto &grp : groups)
      {
        auto &gp = phase_[grp];
        gp.from_event = true;
        if (gp.phase != p)
        {
          gp.phase = p;
          group_status(grp, t);
        }
      }
    }
    else if constexpr (T == 'L')
    {
      stamp(m.TimestampNanoseconds);
      inst_.tick_row(m.PriceTickSizeTableId, m.PriceTickSize, m.PriceStart);
    }
    else if constexpr (T == 'R')
    {
      const uint64_t t = stamp(m.TimestampNanoseconds);
      Instrument *i = inst_.directory(text(m.OrderbookId), text(m.OrderbookCode), text(m.Group),
                                      book_key(m.OrderbookId, m.Group), m.RoundLotSize,
                                      m.PriceTickSizeTableId, m.PriceDecimals,
                                      m.UpperPriceLimit, m.LowerPriceLimit);
      bfile::l3_lim_t r{};
      r.typ = en::l3::LIM;
      r.venue = cfg_.venue;
      r.securityID = i->asset;
      r.pxh = double(i->upper) / 10.0;
      r.pxl = double(i->lower) / 10.0;
      emit(i->asset, r);
      send_status(*i, t);
    }
    else if constexpr (T == 'H')
    {
      const uint64_t t = stamp(m.TimestampNanoseconds);
      Instrument *i = inst_.find(book_key(m.OrderbookId, m.Group));
      if (!i)
      {
        ++st_.unknown_book;
        return;
      }
      i->trading_state = m.TradingState;
      send_status(*i, t);
    }
    else if constexpr (T == 'Y')
    {
      stamp(m.TimestampNanoseconds);
      if (Instrument *i = inst_.find(book_key(m.OrderbookId, m.Group)))
        i->short_sell = m.ShortSellingState;
      else
        ++st_.unknown_book;
    }
    else if constexpr (T == 'A' || T == 'F')
    {
      const uint64_t t = stamp(m.TimestampNanoseconds);
      Instrument *i = inst_.find(book_key(m.OrderbookId, m.Group));
      if (m.OrderNumber == 0)
      {
        ++st_.reference_prices;   // a reference price, not an order (ITCH 4.7.1)
        if (i)
          i->ref_price = m.Price;
        return;
      }
      if (!i)
      {
        ++st_.unknown_book;
        return;
      }
      char ot = ' ';
      if constexpr (T == 'F')
        ot = m.OrderType;
      add(*i, m.OrderNumber, m.BuySellIndicator, m.Quantity, m.Price, ot, t);
    }
    else if constexpr (T == 'E')
    {
      execute(m.OrderNumber, m.ExecutedQuantity, stamp(m.TimestampNanoseconds));
    }
    else if constexpr (T == 'D')
    {
      const uint64_t t = stamp(m.TimestampNanoseconds);
      if (orders_.find(m.OrderNumber) == orders_.end())
      {
        ++st_.unknown_delete;
        return;
      }
      remove(m.OrderNumber, t);
    }
    else if constexpr (T == 'U')
    {
      replace(m.OriginalOrderNumber, m.NewOrderNumber, m.Quantity, m.Price,
              stamp(m.TimestampNanoseconds));
    }
  }

  void Dispatcher::seconds(uint32_t s)
  {
    // A night session may wrap its seconds at midnight or count past 86,400
    // (feed.md section 10); adding a day when they go backward gives the same
    // time either way.
    if (have_secs_ && s < secs_)
    {
      day_add_ += 86400;
      ++st_.seconds_wraps;
    }
    secs_ = s;
    have_secs_ = true;
    last_ns_ = 0;   // now() is the start of the new second until the next message
  }

  uint64_t Dispatcher::stamp(uint32_t ns)
  {
    last_ns_ = ns;
    uint64_t t = time_of(ns);
    if (snapshot_)
      return t - 1;   // OB keeps live records of the snapshot's microsecond (section 7)
    if (t < last_time_)
    {
      ++st_.time_backsteps;
      st_.max_backstep_ns = std::max(st_.max_backstep_ns, last_time_ - t);
    }
    if (t > last_time_)
    {
      if (cfg_.check_cross && !touched_.empty())
        check_crossed();
      last_time_ = t;
    }
    return t;
  }

  void Dispatcher::touch(Instrument *i, uint64_t)
  {
    if (cfg_.check_cross && !snapshot_)
      touched_.insert(i);
  }

  void Dispatcher::check_crossed()
  {
    for (Instrument *i : touched_)
    {
      const Book &b = i->book;
      if (b.bids.empty() || b.asks.empty())
        continue;
      ++st_.crossed_checks;
      const uint32_t bb = b.best_bid(), ba = b.best_ask();
      const uint8_t s = status_of(*i);
      if (bb > ba)
      {
        if (s == status::Trading)
        {
          if (st_.crossed_at_17++ < 10)
            std::cerr << "jnx: crossed book " << i->sicc << "." << i->group << " bid " << bb
                      << " ask " << ba << " at " << last_time_ << " in status 17" << std::endl;
        }
        else
          ++st_.crossed_other;
      }
      else if (bb == ba && s == status::Trading)
        ++st_.locked_at_17;
    }
    touched_.clear();
  }

  Phase Dispatcher::phase_of(const std::string &group) const
  {
    auto it = phase_.find(group);
    return it == phase_.end() ? Phase::Unknown : it->second.phase;
  }

  uint8_t Dispatcher::status_of(const Instrument &i) const
  {
    if (i.trading_state == 'V')
      return status::Halt;
    if (i.trading_state != 'T')
      return status::NotAvailable;   // not in the spin: suspended (ITCH 4.5)
    switch (phase_of(i.group))
    {
    case Phase::Open: return status::Trading;
    case Phase::Closed: return status::Close;
    default: return status::PreOpen;
    }
  }

  void Dispatcher::send_status(Instrument &i, uint64_t t)
  {
    if (!i.listed)
      return;
    const uint8_t s = status_of(i);
    if (s == i.status_sent)
      return;
    i.status_sent = s;
    bfile::l3_sst_t r{};
    r.typ = en::l3::SST;
    r.venue = cfg_.venue;
    r.txtim = t;
    r.sendtim = t;
    r.securityID = i.asset;
    r.tradingStatus = s;
    emit(i.asset, r);
  }

  void Dispatcher::group_status(const std::string &group, uint64_t t)
  {
    for (Instrument *i : inst_.group(group))
      send_status(*i, t);
  }

  void Dispatcher::clock_phases(uint64_t t)
  {
    for (const auto &[name, books] : inst_.groups())
    {
      auto &gp = phase_[name];
      if (gp.from_event)
        continue;
      const Phase p = clock_phase(name, seconds());
      if (p != gp.phase)
      {
        gp.phase = p;
        group_status(name, t);
      }
    }
  }

  bfile::l3_mbo_v2_t Dispatcher::mbo(const Order &o, uint8_t action, uint64_t t) const
  {
    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = t;
    r.sendingTime = t;
    r.orderUpdateAction = action;
    r.securityID = o.inst->asset;
    r.orderID = o.oid;
    r.priority = o.priority;
    r.pxd = double(o.px) / 10.0;   // 1 implied decimal (ITCH 3, 4.4)
    r.displayQty = o.qty;
    r.side = o.side == 'B' ? '0' : '1';
    r.endOfEvent = true;           // no transaction marker in ITCH (section 7)
    r.recovery = snapshot_;
    r.pkt_seq_num = uint32_t(pkt_);
    return r;
  }

  void Dispatcher::check_price(const Instrument &i, uint32_t px)
  {
    if (i.unit > 1 && px % i.unit)
      ++st_.px_off_unit;
    const uint32_t tick = inst_.tick_at(i.table, px);
    if (tick > 1 && px % tick)
      ++st_.px_off_tick;
    if (i.upper && (px > i.upper || px < i.lower))
      ++st_.px_out_of_limits;
  }

  void Dispatcher::add(Instrument &i, uint64_t number, char side, uint32_t qty, uint32_t px,
                       char order_type, uint64_t t)
  {
    if (orders_.count(number))
    {
      // Decision 1: Order Numbers are assumed not to repeat on one stream.
      if (st_.dup_add++ < 10)
        std::cerr << "jnx: Order Number " << number << " added while live" << std::endl;
      return;
    }
    if (number > kNumberMask && st_.wide_order_number++ < 10)
      std::cerr << "jnx: Order Number " << number << " needs more than 58 bits" << std::endl;
    Order o;
    o.number = number;
    o.oid = (uint64_t(cfg_.stream) << 58) | (number & kNumberMask);
    o.priority = seq_;
    o.inst = &i;
    o.px = px;
    o.qty = qty;
    o.side = side == 'S' ? 'S' : 'B';
    o.order_type = order_type;
    auto &ord = orders_.emplace(number, o).first->second;
    i.book.insert(&ord);
    check_price(i, px);
    emit(i.asset, mbo(ord, 0, t));
    touch(&i, t);
  }

  void Dispatcher::execute(uint64_t number, uint32_t qty, uint64_t t)
  {
    auto it = orders_.find(number);
    if (it == orders_.end())
    {
      ++st_.unknown_exec;
      return;
    }
    Order &o = it->second;
    Instrument &i = *o.inst;
    if (qty > o.qty)
      ++st_.over_exec;
    const uint32_t rem = qty >= o.qty ? 0 : o.qty - qty;

    // The print: E names the resting order, so the aggressor is the other side
    // (CME encoding 1 buy, 2 sell), and the price is the resting order's.
    bfile::l3_mbp_trd_t p{};
    p.typ = en::l3::MBPT;
    p.venue = cfg_.venue;
    p.transactTime = t;
    p.sendingTime = t;
    p.securityID = i.asset;
    p.pxd = double(o.px) / 10.0;
    p.side = o.side == 'B' ? '0' : '1';
    p.aggresorSide = o.side == 'B' ? 2 : 1;
    p.sz = int32_t(qty);
    p.numorders = 1;
    p.endOfEvent = true;
    p.lastTrade = true;
    emit(i.asset, p);

    bfile::l3_mbo_trd_v2_t x{};
    x.typ = en::l3::MBOT_V2;
    x.venue = cfg_.venue;
    x.transactTime = t;
    x.sendingTime = t;
    x.lastQty = int32_t(qty);
    x.orderID = o.oid;
    x.lastTrade = true;
    x.endOfEvent = true;
    x.pkt_seq_num = uint32_t(pkt_);
    emit(i.asset, x);

    if (rem)
    {
      i.book.resize(&o, rem);
      emit(i.asset, mbo(o, 1, t));
    }
    else
    {
      // A full execution removes the order; no D follows (section 6).
      emit(i.asset, mbo(o, 2, t));
      i.book.remove(&o);
      orders_.erase(it);
    }
    touch(&i, t);
  }

  void Dispatcher::remove(uint64_t number, uint64_t t)
  {
    auto it = orders_.find(number);
    Order &o = it->second;
    Instrument &i = *o.inst;
    emit(i.asset, mbo(o, 2, t));
    i.book.remove(&o);
    orders_.erase(it);
    touch(&i, t);
  }

  void Dispatcher::replace(uint64_t original, uint64_t number, uint32_t qty, uint32_t px,
                           uint64_t t)
  {
    if (orders_.find(original) == orders_.end())
    {
      ++st_.unknown_replace;
      return;
    }
    if (number != original && orders_.count(number))
    {
      if (st_.replace_to_existing++ < 10)
        std::cerr << "jnx: replace " << original << " -> " << number
                  << ": the new number is live; deleting it" << std::endl;
      remove(number, t);
    }
    // The replacement keeps the side, book and orderID of the original (ITCH
    // 4.10; section 7). Priority: OB's rule (decision 2).
    auto nh = orders_.extract(original);
    nh.key() = number;
    Order &o = nh.mapped();
    o.number = number;
    Instrument &i = *o.inst;
    if (px == o.px && qty <= o.qty)
    {
      ++st_.replace_keep_place;
      i.book.resize(&o, qty);
    }
    else
    {
      ++st_.replace_lose_place;
      i.book.remove(&o);
      o.px = px;
      o.qty = qty;
      i.book.insert(&o);
    }
    Order &placed = orders_.insert(std::move(nh)).position->second;
    check_price(i, px);
    emit(i.asset, mbo(placed, 1, t));
    touch(&i, t);
  }

  void Dispatcher::end_of_packet(uint64_t first_seq)
  {
    bfile::l3_eop_t r{};
    r.typ = en::l3::EOP;
    r.venue = cfg_.venue;
    r.msgSeqNum = uint32_t(first_seq);   // low 32 bits of the 64-bit sequence number
    r.sendingTime = now();
    ++st_.records[en::l3::EOP];
    sink_.recorder(r);
  }

  void Dispatcher::reset()
  {
    bfile::l3_chr_v2_t r{};
    r.typ = en::l3::CHR_V2;
    r.venue = cfg_.venue;
    ++st_.records[en::l3::CHR_V2];
    ++st_.resets;
    sink_.all_books(r);
    orders_.clear();
    inst_.reset();
    phase_.clear();
    touched_.clear();
    last_time_ = 0;
    forget_time();
  }

  void Dispatcher::gap()
  {
    bfile::l3_gap_v2_t r{};
    r.typ = en::l3::GAP_V2;
    r.venue = cfg_.venue;
    ++st_.records[en::l3::GAP_V2];
    sink_.all_books(r);
  }

  void Dispatcher::begin_snapshot()
  {
    snapshot_ = true;
    touched_.clear();
  }

  void Dispatcher::end_snapshot(bool clock)
  {
    snapshot_ = false;
    last_time_ = now();
    if (clock)
      clock_phases(now());
  }

  void Dispatcher::emit(int32_t asset, const bfile::l3_t &r)
  {
    ++st_.records[std::visit([](const auto &x) { return uint32_t(x.typ); }, r)];
    sink_.book(asset, r);
  }
}
