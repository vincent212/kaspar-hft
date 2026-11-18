/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Cboe Europe Multicast PITCH to Kaspar L3 records. NOT CERTIFIED. Citations:
// EU = Cboe Titanium Europe Multicast PITCH Specification 6.69 (section, page).

#include "cboe_europe/PitchEuHandler.hpp"

#include <algorithm>
#include <climits>
#include <iterator>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "cboe_europe/gen/cboe_europe.hpp"
#include "cboe_europe/gen/pitch_us.hpp"

namespace cboe_europe
{
  namespace pitch = cboe::pitch::from_cboe_eu;

  namespace
  {
    // Days since 1970-01-01 (H. Hinnant's days_from_civil).
    int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
    {
      y -= m <= 2;
      const int64_t era = (y >= 0 ? y : y - 399) / 400;
      const unsigned yoe = unsigned(y - era * 400);
      const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + int64_t(doe) - 719468;
    }

    // Day of the month of the last Sunday of a 31-day month.
    unsigned last_sunday(int64_t y, unsigned m)
    {
      const int64_t days = days_from_civil(y, m, 31);
      const unsigned wd = unsigned((days % 7 + 7 + 4) % 7); // 0 Sunday; 1970-01-01 was a Thursday
      return 31 - wd;
    }

    bool is_book_record(const bfile::l3_t &r)
    {
      return std::holds_alternative<bfile::l3_mbo_v2_t>(r) || std::holds_alternative<bfile::l3_mbo_trd_v2_t>(r) ||
             std::holds_alternative<bfile::l3_mbp_trd_t>(r);
    }

    void set_end_of_event(bfile::l3_t &r)
    {
      std::visit([](auto &x) {
        if constexpr (requires { x.endOfEvent; })
          x.endOfEvent = true;
      }, r);
    }

    // MMT Level 2 (trading mode) values that are auctions (EU 4.17, p.32).
    bool auction_mode(char level1, char level2)
    {
      return level1 == '5' || level2 == '1' || level2 == 'O' || level2 == 'K' || level2 == 'I' || level2 == 'U' ||
             level2 == 'P';
    }
  }

  uint64_t london_midnight_ns(uint32_t yyyymmdd)
  {
    const int64_t y = yyyymmdd / 10000;
    const unsigned m = (yyyymmdd / 100) % 100, d = yyyymmdd % 100;
    if (m < 1 || m > 12 || d < 1 || d > 31)
      throw std::invalid_argument("trade date is not YYYYMMDD");
    const int64_t days = days_from_civil(y, m, d);
    // British Summer Time runs from 01:00 UTC on the last Sunday of March to
    // 01:00 UTC on the last Sunday of October. Local midnight falls in BST on
    // the days after the March change up to and including the October change day.
    const int64_t mar = days_from_civil(y, 3, last_sunday(y, 3));
    const int64_t oct = days_from_civil(y, 10, last_sunday(y, 10));
    const bool bst = days > mar && days <= oct;
    return uint64_t(days * 86400 - (bst ? 3600 : 0)) * 1000000000ull;
  }

  std::unordered_map<std::string, int32_t> load_instruments(const std::string &path)
  {
    std::ifstream in(path);
    if (!in)
      throw std::runtime_error("cannot open " + path);
    std::unordered_map<std::string, int32_t> out;
    std::string line;
    while (std::getline(in, line))
    {
      if (auto h = line.find('#'); h != std::string::npos)
        line.resize(h);
      const auto c = line.find(',');
      if (c == std::string::npos)
        continue;
      std::string sym = line.substr(0, c);
      while (!sym.empty() && (sym.back() == ' ' || sym.back() == '\t'))
        sym.pop_back();
      while (!sym.empty() && (sym.front() == ' ' || sym.front() == '\t'))
        sym.erase(sym.begin());
      if (sym.empty())
        continue;
      out[sym] = int32_t(std::stol(line.substr(c + 1)));
    }
    return out;
  }

  // Phases from the Cboe Europe Participant Manual v59, 2.16 "Trading Phases",
  // pp.32-33; Y and H from EU 4.13, p.27 only.
  uint8_t trading_status(char s)
  {
    switch (s)
    {
    case 'T': // Trading: continuous trading
    case 'V': // Volatility Interruption: static collar reached, continuous trading goes on inside it
    case 'N': // No Reference Price: dark matching unavailable; the lit book trades
      return 17;
    case 'O': // Opening Auction: no automated executions
    case 'E': // Closing Auction
    case 'M': // Market Order Imbalance Extension
    case 'P': // Price Monitoring Extension
    case 'I': // Volatility Auction
    case 'Y': // Halt Auction
    case 'U': // Post Market Crossing (Cboe Closing Cross, uncrosses every 15 s): review.md decision 3
      return 21;
    case 'C': // Closed
      return 4;
    case 'H': // Halt (reserved for future use)
      return 2;
    default: // S Suspension (orders cancelled), R Off-book Reporting (no order entry), unknown
      return 18;
    }
  }

  PitchEuHandler::PitchEuHandler(Config cfg, Emit emit, OnGap on_gap)
      : cfg_(std::move(cfg)), emit_(std::move(emit)), on_gap_(std::move(on_gap)),
        midnight_ns_(london_midnight_ns(cfg_.trade_date))
  {
  }

  bool PitchEuHandler::on_packet(const char *buf, size_t len)
  {
    cboe::unit::from_cboe::SequencedUnitHeader h;
    if (len < sizeof h)
      return false;
    std::memcpy(&h, buf, sizeof h);
    if (h.HdrLength < sizeof h || h.HdrLength > len)
      return false;
    ++stats_.packets;
    const uint8_t u = h.HdrUnit;
    const uint32_t s = h.HdrSequence;
    Unit &U = units_[u];

    if (h.HdrCount == 0)
    {
      // Real-time heartbeats carry the next sequence; gap-channel and off-hours
      // heartbeats carry 0 (EU 2.7, p.11).
      ++stats_.heartbeats;
      if (u != 0 && s != 0 && U.state == UnitState::live && int32_t(s - U.expected) > 0)
        gap(u, U.expected, s - U.expected);
      return true;
    }
    if (s == 0 || u == 0)
    {
      stats_.unsequenced += h.HdrCount;
      return true;
    }

    const uint64_t applied_before = stats_.applied;
    const char *p = buf + sizeof h;
    size_t left = h.HdrLength - sizeof h;
    for (unsigned i = 0; i < h.HdrCount; ++i)
    {
      if (left < 2 || uint8_t(p[0]) < 2 || uint8_t(p[0]) > left)
      {
        ++stats_.malformed;
        break;
      }
      const size_t n = uint8_t(p[0]);
      process(u, s + i, std::string_view(p, n));
      p += n;
      left -= n;
    }

    if (stats_.applied != applied_before)
    {
      bfile::l3_eop_t e{};
      e.typ = en::l3::EOP;
      e.venue = cfg_.venue;
      e.msgSeqNum = s;
      e.sendingTime = U.last_tx;
      if (U.in_txn)
        U.pending.emplace_back(-1, e);
      else
      {
        ++stats_.records;
        emit_(-1, e);
      }
    }
    return true;
  }

  void PitchEuHandler::process(uint8_t u, uint32_t seq, std::string_view msg)
  {
    Unit &U = units_[u];
    const int32_t d = int32_t(seq - U.expected);
    if (d < 0)
    {
      ++stats_.duplicates;
      return;
    }
    if (U.state != UnitState::spinning && d == 0)
    {
      apply(u, seq, msg.data(), msg.size(), false);
      U.expected = seq + 1;
      drain(u);
      return;
    }
    if (U.state == UnitState::spinning && int32_t(seq - U.spin_seq) <= 0)
    {
      ++stats_.duplicates; // already in the spin
      return;
    }
    if (!U.buffer.emplace(seq, std::string(msg)).second)
    {
      ++stats_.duplicates;
      return;
    }
    ++stats_.buffered;
    if (U.buffer.size() > cfg_.max_buffer)
    {
      // The oldest buffered message is lost: retransmission can no longer
      // close the gap by itself; report once, until the next spin.
      U.buffer.erase(U.buffer.begin());
      ++stats_.overflows;
      if (!U.overflowed && U.state != UnitState::spinning)
      {
        U.overflowed = true;
        if (on_gap_)
          on_gap_(u, U.expected, 0);
      }
    }
    if (U.state == UnitState::live)
      gap(u, U.expected, seq - U.expected);
  }

  void PitchEuHandler::drain(uint8_t u)
  {
    Unit &U = units_[u];
    while (!U.buffer.empty())
    {
      auto it = U.buffer.begin();
      const int32_t d = int32_t(it->first - U.expected);
      if (d < 0)
      {
        U.buffer.erase(it);
        continue;
      }
      if (d > 0)
        break;
      apply(u, it->first, it->second.data(), it->second.size(), false);
      U.expected = it->first + 1;
      U.buffer.erase(it);
    }
    if (U.state == UnitState::gapped && U.buffer.empty())
      U.state = UnitState::live;
  }

  void PitchEuHandler::gap(uint8_t u, uint32_t first, uint32_t count)
  {
    Unit &U = units_[u];
    U.in_txn = false;
    flush(U);
    bfile::l3_gap_v2_t g{};
    g.typ = en::l3::GAP_V2;
    g.venue = cfg_.venue;
    to_books(u, g);
    ++stats_.gaps;
    U.state = UnitState::gapped;
    if (on_gap_)
      on_gap_(u, first, count);
  }

  bool PitchEuHandler::spin_usable(uint8_t u, uint32_t image_seq) const
  {
    const Unit &U = units_[u];
    const uint32_t need = image_seq + 1;
    if (int32_t(need - U.expected) < 0)
      return false; // messages after the image were applied to the old book and are gone
    uint32_t max_seen = U.expected - 1;
    if (!U.buffer.empty() && int32_t(U.buffer.rbegin()->first - max_seen) > 0)
      max_seen = U.buffer.rbegin()->first;
    if (int32_t(max_seen - need) < 0)
      return true; // nothing after the image received yet
    const auto lo = U.buffer.lower_bound(need);
    const auto hi = U.buffer.upper_bound(max_seen);
    return size_t(std::distance(lo, hi)) == size_t(max_seen - need) + 1;
  }

  void PitchEuHandler::spin_begin(uint8_t u, uint32_t image_seq)
  {
    Unit &U = units_[u];
    U.in_txn = false;
    flush(U);
    bfile::l3_chr_v2_t c{};
    c.typ = en::l3::CHR_V2;
    c.venue = cfg_.venue;
    to_books(u, c);
    U.orders.clear();
    U.state = UnitState::spinning;
    U.spin_seq = image_seq;
    U.closed = false;
    U.overflowed = false;
    ++stats_.spins;
    for (auto it = U.buffer.begin(); it != U.buffer.end();)
      it = int32_t(it->first - image_seq) <= 0 ? U.buffer.erase(it) : std::next(it);
  }

  void PitchEuHandler::spin_message(uint8_t u, const char *buf, size_t len)
  {
    if (units_[u].state != UnitState::spinning)
      return;
    apply(u, 0, buf, len, true);
  }

  void PitchEuHandler::spin_finished(uint8_t u)
  {
    Unit &U = units_[u];
    if (U.state != UnitState::spinning)
      return;
    U.in_txn = false;
    flush(U);
    U.expected = U.spin_seq + 1;
    U.state = UnitState::gapped;
    drain(u);
    if (!U.buffer.empty())
    {
      const uint32_t first = U.expected;
      if (on_gap_)
        on_gap_(u, first, U.buffer.begin()->first - first);
    }
  }

  int32_t PitchEuHandler::sec_of(uint8_t u, std::string_view sym)
  {
    auto it = cfg_.symbols.find(std::string(sym));
    if (it == cfg_.symbols.end())
    {
      ++stats_.unknown_symbols;
      return -1;
    }
    units_[u].books.insert(it->second);
    return it->second;
  }

  uint64_t PitchEuHandler::stamp(Unit &U, uint32_t time_offset)
  {
    if (U.pending.empty() || !U.event_tx)
    {
      const uint64_t tx = midnight_ns_ + U.time_s * 1000000000ull + time_offset;
      U.event_tx = std::max(tx, U.last_tx + 1);
      U.last_tx = U.event_tx;
    }
    return U.event_tx;
  }

  void PitchEuHandler::push(Unit &U, int32_t sec, const bfile::l3_t &r)
  {
    U.pending.emplace_back(sec, r);
  }

  void PitchEuHandler::end_event(Unit &U)
  {
    if (!U.in_txn)
      flush(U);
  }

  void PitchEuHandler::flush(Unit &U)
  {
    // endOfEvent on the last record of the event for each book: a transaction
    // block may touch several symbols (EU 4.11, p.26), and each OB sees only
    // its own records.
    std::set<int32_t> marked;
    for (auto it = U.pending.rbegin(); it != U.pending.rend(); ++it)
      if (is_book_record(it->second) && marked.insert(it->first).second)
        set_end_of_event(it->second);
    for (auto &[sec, r] : U.pending)
    {
      ++stats_.records;
      emit_(sec, r);
    }
    U.pending.clear();
    U.event_tx = 0;
  }

  void PitchEuHandler::to_books(uint8_t u, const bfile::l3_t &r)
  {
    for (int32_t sec : units_[u].books)
    {
      ++stats_.records;
      emit_(sec, r);
    }
  }

  void PitchEuHandler::print(Unit &U, int32_t sec, uint64_t tx, double px, int64_t qty, uint8_t aggressor)
  {
    bfile::l3_mbp_trd_t t{};
    t.typ = en::l3::MBPT;
    t.venue = cfg_.venue;
    t.transactTime = tx;
    t.sendingTime = tx;
    t.securityID = sec;
    t.pxd = px;
    t.sz = int32_t(qty);
    t.numorders = 1;
    t.aggresorSide = aggressor;
    t.lastTrade = true;
    push(U, sec, t);
  }

  void PitchEuHandler::add(uint8_t u, uint32_t seq, uint32_t to, uint64_t id, char side, uint32_t qty,
                           std::string_view sym, double px, bool recovery)
  {
    Unit &U = units_[u];
    const int32_t sec = sec_of(u, sym);
    const char s = side == 'B' ? '0' : '1';
    U.orders[id] = Order{sec, s, px, qty};
    if (sec < 0)
      return;
    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = r.sendingTime = stamp(U, to);
    r.orderUpdateAction = 0;
    r.securityID = sec;
    r.orderID = id;
    r.priority = seq;
    r.pxd = px;
    r.displayQty = qty;
    r.side = s;
    r.recovery = recovery;
    push(U, sec, r);
    end_event(U);
  }

  void PitchEuHandler::change(uint8_t u, uint32_t to, uint64_t id, uint32_t qty, double px)
  {
    Unit &U = units_[u];
    auto it = U.orders.find(id);
    if (it == U.orders.end())
    {
      ++stats_.unknown_orders;
      return;
    }
    if (qty == 0)
    {
      remove(u, to, id);
      return;
    }
    Order &o = it->second;
    o.qty = qty;
    o.px = px;
    if (o.sec_id < 0)
      return;
    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = r.sendingTime = stamp(U, to);
    r.orderUpdateAction = 1;
    r.securityID = o.sec_id;
    r.orderID = id;
    r.pxd = px;
    r.displayQty = qty;
    r.side = o.side;
    push(U, o.sec_id, r);
    end_event(U);
  }

  void PitchEuHandler::remove(uint8_t u, uint32_t to, uint64_t id)
  {
    Unit &U = units_[u];
    auto it = U.orders.find(id);
    if (it == U.orders.end())
    {
      ++stats_.unknown_orders;
      return;
    }
    const Order o = it->second;
    U.orders.erase(it);
    if (o.sec_id < 0)
      return;
    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = r.sendingTime = stamp(U, to);
    r.orderUpdateAction = 2;
    r.securityID = o.sec_id;
    r.orderID = id;
    r.pxd = o.px;
    r.side = o.side;
    push(U, o.sec_id, r);
    end_event(U);
  }

  void PitchEuHandler::execute(uint8_t u, uint32_t to, uint64_t id, uint32_t executed, const uint32_t *remaining,
                               const double *price, const char *flags)
  {
    Unit &U = units_[u];
    auto it = U.orders.find(id);
    if (it == U.orders.end())
    {
      ++stats_.unknown_orders;
      return;
    }
    Order &o = it->second;
    uint32_t left;
    if (remaining)
      left = *remaining;
    else if (executed > o.qty)
    {
      ++stats_.over_executions;
      left = 0;
    }
    else
      left = o.qty - executed;
    const int32_t sec = o.sec_id;
    const char side = o.side;
    const double book_px = o.px;
    if (left == 0)
      U.orders.erase(it);
    else
      o.qty = left;
    if (sec < 0)
      return;

    const uint64_t tx = stamp(U, to);
    const uint8_t aggressor = auction_mode(flags[0], flags[1]) ? 0 : (side == '0' ? 2 : 1);
    print(U, sec, tx, price ? *price : book_px, executed, aggressor);

    bfile::l3_mbo_trd_v2_t x{};
    x.typ = en::l3::MBOT_V2;
    x.venue = cfg_.venue;
    x.transactTime = x.sendingTime = tx;
    x.lastQty = int32_t(executed);
    x.orderID = id;
    push(U, sec, x);

    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = r.sendingTime = tx;
    r.orderUpdateAction = left == 0 ? 2 : 1;
    r.securityID = sec;
    r.orderID = id;
    r.pxd = book_px;
    r.displayQty = left;
    r.side = side;
    push(U, sec, r);
    end_event(U);
  }

  void PitchEuHandler::apply(uint8_t u, uint32_t seq, const char *buf, size_t len, bool recovery)
  {
    auto d = pitch::decode(buf, len);
    if (d.st == cboe::status::unknown)
    {
      ++stats_.unknown;
      return;
    }
    if (d.st != cboe::status::ok)
    {
      ++stats_.malformed;
      return;
    }
    ++stats_.applied;
    Unit &U = units_[u];
    constexpr double L = 1e4, S = 1e2; // Binary Long Price on BXE/CXE, Binary Short Price (EU 2.2, p.10)

    std::visit([&](const auto &m) {
      using T = std::decay_t<decltype(m)>;
      if constexpr (std::is_same_v<T, pitch::TimeMessage>)
        U.time_s = m.Time;
      else if constexpr (std::is_same_v<T, pitch::AddOrderLong>)
        add(u, seq, m.TimeOffset, m.OrderId, m.SideIndicator, m.Quantity, m.get_Symbol(), double(m.Price) / L, recovery);
      else if constexpr (std::is_same_v<T, pitch::AddOrderShort>)
        add(u, seq, m.TimeOffset, m.OrderId, m.SideIndicator, m.Quantity, m.get_Symbol(), double(m.Price) / S, recovery);
      else if constexpr (std::is_same_v<T, pitch::AddOrderExpanded>)
        ++stats_.si_quotes; // non-executable SI quote, SI feed only (EU 4.3.3, p.16)
      else if constexpr (std::is_same_v<T, pitch::OrderExecuted>)
        execute(u, m.TimeOffset, m.OrderId, m.ExecutedShares, nullptr, nullptr, m.ExecutionFlags);
      else if constexpr (std::is_same_v<T, pitch::OrderExecutedAtPriceSize>)
      {
        const uint32_t rem = m.RemainingShares;
        const double px = double(m.Price) / L;
        execute(u, m.TimeOffset, m.OrderId, m.ExecutedShares, &rem, &px, m.ExecutionFlags);
      }
      else if constexpr (std::is_same_v<T, pitch::ReduceSizeLong> || std::is_same_v<T, pitch::ReduceSizeShort>)
      {
        auto it = U.orders.find(m.OrderId);
        if (it == U.orders.end())
          ++stats_.unknown_orders;
        else
        {
          const uint32_t c = m.CancelledShares;
          change(u, m.TimeOffset, m.OrderId, c >= it->second.qty ? 0 : it->second.qty - c, it->second.px);
        }
      }
      else if constexpr (std::is_same_v<T, pitch::ModifyOrderLong>)
        change(u, m.TimeOffset, m.OrderId, m.Shares, double(m.Price) / L);
      else if constexpr (std::is_same_v<T, pitch::ModifyOrderShort>)
        change(u, m.TimeOffset, m.OrderId, m.Shares, double(m.Price) / S);
      else if constexpr (std::is_same_v<T, pitch::DeleteOrder>)
        remove(u, m.TimeOffset, m.OrderId);
      else if constexpr (std::is_same_v<T, pitch::TradeLong> || std::is_same_v<T, pitch::TradeShort>)
      {
        const int32_t sec = sec_of(u, m.get_Symbol());
        if (sec >= 0)
        {
          const double px = double(m.Price) / (std::is_same_v<T, pitch::TradeLong> ? L : S);
          print(U, sec, stamp(U, m.TimeOffset), px, m.Shares, 0);
          end_event(U);
        }
      }
      else if constexpr (std::is_same_v<T, pitch::TradeExtended>)
      {
        const char *f = m.ExtendedTradeFlags;
        bool take = m.Price > 0 && m.Shares <= uint64_t(INT32_MAX);
        if (cfg_.extended_prints == ExtendedPrints::none)
          take = false;
        else if (cfg_.extended_prints == ExtendedPrints::on_book)
          take = take && (f[0] == '1' || f[0] == '3' || f[0] == '5') && f[3] == '-' && f[5] == '-' && f[9] == 'P';
        const int32_t sec = take ? sec_of(u, m.get_Symbol()) : -1;
        if (sec >= 0)
        {
          ++stats_.extended_printed;
          print(U, sec, stamp(U, m.TimeOffset), double(m.Price) / L, int64_t(m.Shares), 0);
          end_event(U);
        }
        else
          ++stats_.extended_skipped;
      }
      else if constexpr (std::is_same_v<T, pitch::TradeBreak>)
        ++stats_.trade_breaks;
      else if constexpr (std::is_same_v<T, pitch::TransactionBegin>)
      {
        if (cfg_.honour_transactions)
        {
          flush(U);
          U.in_txn = true;
        }
      }
      else if constexpr (std::is_same_v<T, pitch::TransactionEnd>)
      {
        if (cfg_.honour_transactions)
        {
          U.in_txn = false;
          flush(U);
        }
      }
      else if constexpr (std::is_same_v<T, pitch::TradingStatus>)
      {
        const int32_t sec = sec_of(u, m.get_Symbol());
        if (sec >= 0)
        {
          bfile::l3_sst_t r{};
          r.typ = en::l3::SST;
          r.venue = cfg_.venue;
          r.txtim = r.sendtim = stamp(U, m.TimeOffset);
          r.securityID = sec;
          r.tradingStatus = trading_status(m.Status);
          push(U, sec, r);
          end_event(U);
        }
      }
      else if constexpr (std::is_same_v<T, pitch::UnitClear>)
      {
        U.in_txn = false;
        flush(U);
        bfile::l3_chr_v2_t c{};
        c.typ = en::l3::CHR_V2;
        c.venue = cfg_.venue;
        to_books(u, c);
        U.orders.clear();
      }
      else if constexpr (std::is_same_v<T, pitch::EndOfSession>)
        U.closed = true;
      // TradeUnknownSymbol (TRF), Statistics, AuctionUpdate, AuctionSummary:
      // no record (feed.md section 7).
    }, d.msg);
  }
}
