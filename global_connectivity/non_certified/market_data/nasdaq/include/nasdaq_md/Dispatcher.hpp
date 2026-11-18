#pragma once
// ITCH 5.0 dispatcher: decoded messages -> Kaspar L3 records, as section 7 of
// feed.md says and review.md's decisions (owner, 2026-11-18) settle.
//
// One instance decodes one market's feed (Nasdaq, BX or PSX). It keeps the
// order-reference map (the OwnBook), the instrument directory and the derived
// trading status, and hands each record to a Sink: to one book (by asset id)
// or to the recorder. It does not know about transports: the sequencer feeds
// it messages in sequence order (on_itch), a GLIMPSE spin feeds it recovery
// messages (on_glimpse, between begin_recovery and end_recovery).
//
// The message structs of itch::from_nasdaq (with the MoldUDP64 length) and
// glimpse::from_nasdaq (without) have the same field names, so one template
// handles both.
#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "glimpse50.hpp"
#include "itch50.hpp"
#include "nasdaq_md/Instruments.hpp"
#include "nasdaq_md/OwnBook.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace nasdaq_md
{
// Where records go. to_book is called for records of one routed security (its
// asset id); to_recorder for every record that is sent anywhere, plus the
// end-of-packet record, which goes to the recorder only (as the CME handler
// does, handler_if::EndOfPacket).
struct Sink
{
  virtual ~Sink() = default;
  virtual void to_book(int32_t asset, const bfile::l3_t &r) = 0;
  virtual void to_recorder(const bfile::l3_t &) {}
};

struct DispatcherStats
{
  std::array<uint64_t, 128> msgs{};           // ITCH messages by type code
  std::array<uint64_t, 128> glimpse_msgs{};   // GLIMPSE spin messages by type code
  std::array<uint64_t, 128> unknown_ref{};    // E C X D U referencing no known order
  uint64_t decode_errors = 0;
  uint64_t add_duplicate = 0;                 // Add (or U's new reference) already on the book
  uint64_t exec_over = 0;                     // E/C/X larger than the remaining shares
  uint64_t price_zero = 0;                    // Add with Price 0: logged, not sent
  uint64_t off_tick = 0;                      // routed Add whose price is not on the asset's tick
  uint64_t cross_capped = 0;                  // Q Shares above INT32_MAX
  uint64_t broken = 0;                        // B: logged, no record
  uint64_t ts_backwards = 0;                  // Timestamp below the previous message's
  uint64_t ts_clamped = 0;                    // live records restamped after a recovery
  uint64_t crossed_17 = 0;                    // own book crossed while status 17
  uint64_t locked_17 = 0;                     // own book locked while status 17
  uint64_t boundary_suspect = 0;              // decision 1 check at the snapshot's N
  uint64_t recoveries = 0, resets = 0, gaps = 0;
  uint64_t records_book = 0, records_recorder = 0;
  std::array<uint64_t, 64> records_by_l3{};   // by en::l3 code, records sent to books
};

class Dispatcher
{
public:
  struct Config
  {
    char venue = char(en::x::NASDAQ);   // en::x code of the market
    char market_code = 'Q';             // Operational Halt Market Code of this feed: Q, B, X
    uint64_t midnight_ns = 0;           // epoch ns of midnight ET on the trading date
    bool check_cross = true;            // own-book cross check at timestamp boundaries
    std::function<void(const std::string &)> log;   // diagnostics; may be empty
  };

  Dispatcher(const Config &c, Instruments &ins, Sink &sink)
      : cfg_(c), ins_(ins), sink_(sink) {}

  // ---- packets (MoldUDP64) --------------------------------------------------
  void begin_packet(uint64_t seq, uint64_t recv_ns)
  {
    pkt_seq_ = uint32_t(seq);
    pkt_idx_ = 0;
    recv_ns_ = recv_ns;
  }
  // Sends l3_eop_t to the recorder and l3_eob_t to every book that got a record
  // since the last end of packet (the burst marker OB and the lights read).
  void end_packet()
  {
    bfile::l3_eop_t e{};
    e.typ = en::l3::EOP;
    e.venue = cfg_.venue;
    e.msgSeqNum = pkt_seq_;
    e.sendingTime = last_tx_;
    sink_.to_recorder(e);
    ++stats_.records_recorder;
    flush_eob();
  }

  // ---- messages -------------------------------------------------------------
  // One MoldUDP64 message block (2-byte length, then the ITCH message), with
  // its sequence number.
  void on_itch(const char *p, size_t len, uint64_t seq)
  {
    auto d = nasdaq::itch::from_nasdaq::decode(p, len);
    if (!d)
    {
      ++stats_.decode_errors;
      return;
    }
    seq_ = seq;
    std::visit([&](const auto &m) { on(m, false); }, d.msg);
    if (boundary_seq_ && seq == boundary_seq_)
      boundary_seq_ = 0;
  }

  // One GLIMPSE message (a SoupBinTCP Sequenced Data payload), during a spin.
  // Returns the End of Snapshot sequence number when the message is G, else 0.
  uint64_t on_glimpse(const char *p, size_t len)
  {
    auto d = nasdaq::glimpse::from_nasdaq::decode(p, len);
    if (!d)
    {
      ++stats_.decode_errors;
      return 0;
    }
    uint64_t n = 0;
    std::visit(
        [&](const auto &m) {
          using M = std::decay_t<decltype(m)>;
          ++stats_.glimpse_msgs[uint8_t(M::type) & 127];
          if constexpr (M::type == 'G')
            n = parse_seq(m.SequenceNumber, sizeof m.SequenceNumber);
          else
            on(m, true);
        },
        d.msg);
    return n;
  }

  // ---- resets and recovery --------------------------------------------------
  // A new MoldUDP64 session, End of Messages, or the start of a recovery: CHR
  // to every routed book, the order map and the per-security status cleared
  // (#122). The directory is kept unless new_day.
  void reset(bool new_day)
  {
    ++stats_.resets;
    bfile::l3_chr_v2_t c{};
    c.typ = en::l3::CHR_V2;
    c.venue = cfg_.venue;
    for (auto loc : ins_.routed())
      send(ins_.asset(loc), c);
    book_.clear();
    touched_.clear();
    ins_.clear_status();
    for (auto loc : ins_.routed())
      ins_.at(loc).sent_status = 0;   // OB forgets nothing on CHR, but resend anyway
    if (new_day)
      ins_.clear();
    last_ts_ = 0;
    for (auto loc : ins_.routed())
      update_status(loc);
  }

  void begin_recovery()
  {
    ++stats_.recoveries;
    reset(false);
    recovering_ = true;
    rec_max_tx_ = 0;
  }

  // The spin ended with End of Snapshot N. Decision 1: N is not in the
  // snapshot; live messages from N on are applied. The message numbered N is
  // checked: a reference to an order the spin did not have, or an Add of one it
  // did, is counted in boundary_suspect and logged.
  void end_recovery(uint64_t n)
  {
    recovering_ = false;
    live_floor_ = rec_max_tx_;
    boundary_seq_ = n;
    for (auto loc : ins_.routed())
      update_status(loc);
    flush_eob();
  }

  // A gap the sequencer has not yet closed: GAP to every routed book.
  void gap()
  {
    ++stats_.gaps;
    bfile::l3_gap_v2_t g{};
    g.typ = en::l3::GAP_V2;
    g.venue = cfg_.venue;
    for (auto loc : ins_.routed())
      send(ins_.asset(loc), g);
  }

  const OwnBook &book() const { return book_; }
  const DispatcherStats &stats() const { return stats_; }
  Instruments &instruments() { return ins_; }
  int system_phase() const { return phase_; }
  bool recovering() const { return recovering_; }
  uint64_t transact_time(uint64_t ts) const { return cfg_.midnight_ns + ts; }

  // Derived CME SecurityTradingStatus (feed.md section 7, decision 6): first
  // matching row of the table.
  uint8_t derive_status(const Instrument &i) const
  {
    if (phase_ == 2)
      return 4;                                    // after End of System hours
    if (i.op_halt)
      return 2;                                    // Operational Halt on this market
    if (i.trading_state == 'H' || i.trading_state == 'P')
      return 2;
    if (phase_ == 1 && i.trading_state == 0)
      return 2;                                    // absent from the pre-open spin
    if (i.trading_state == 'Q')
      return 21;
    if (phase_ == 0)
      return 21;                                   // before Start of System hours
    if (i.trading_state == 'T')
      return 17;
    return 21;
  }

private:
  Config cfg_;
  Instruments &ins_;
  Sink &sink_;
  OwnBook book_;
  DispatcherStats stats_;

  int phase_ = 0;                 // 0 before S, 1 S..E, 2 after E
  uint64_t prio_ = 0;             // priority counter
  uint64_t seq_ = 0;              // sequence number of the message being handled
  uint32_t pkt_seq_ = 0, pkt_idx_ = 0;
  uint64_t recv_ns_ = 0;
  uint64_t last_tx_ = 0;          // transactTime of the last record sent
  uint64_t last_ts_ = 0;          // ITCH Timestamp of the last live message
  bool recovering_ = false;
  uint64_t rec_max_tx_ = 0;       // largest transactTime of the recovery records
  uint64_t live_floor_ = 0;       // live records at or below this are restamped
  uint64_t boundary_seq_ = 0;     // N of the last End of Snapshot, until handled
  std::vector<uint16_t> touched_;           // locates with records since the last EOB
  std::vector<uint8_t> touched_flag_ = std::vector<uint8_t>(65536, 0);
  std::vector<uint16_t> xcheck_;            // locates changed since the last timestamp
  std::vector<uint8_t> xcheck_flag_ = std::vector<uint8_t>(65536, 0);

  void logf(const std::string &s)
  {
    if (cfg_.log)
      cfg_.log(s);
  }

  static uint64_t parse_seq(const char *s, size_t n)
  {
    // Padding is not stated (review.md, low impact): trim spaces on both
    // sides; leading zeros are read as digits.
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i)
      if (s[i] >= '0' && s[i] <= '9')
        v = v * 10 + uint64_t(s[i] - '0');
    return v;
  }

  void send(int32_t asset, const bfile::l3_t &r)
  {
    sink_.to_book(asset, r);
    sink_.to_recorder(r);
    ++stats_.records_book;
    ++stats_.records_by_l3[std::visit([](const auto &x) { return unsigned(x.typ); }, r) & 63];
  }

  void touch(uint16_t loc)
  {
    if (!touched_flag_[loc])
    {
      touched_flag_[loc] = 1;
      touched_.push_back(loc);
    }
  }

  void flush_eob()
  {
    bfile::l3_eob_t b{};
    b.typ = en::l3::EOB;
    b.venue = cfg_.venue;
    for (auto loc : touched_)
    {
      touched_flag_[loc] = 0;
      const auto a = ins_.asset(loc);
      if (a >= 0)
        sink_.to_book(a, b);
    }
    touched_.clear();
  }

  // transactTime of a record of the current message.
  uint64_t stamp(uint64_t ts, bool recovery)
  {
    uint64_t tx = cfg_.midnight_ns + ts;
    if (recovery)
    {
      if (tx > rec_max_tx_)
        rec_max_tx_ = tx;
    }
    else if (live_floor_)
    {
      // OB rejects a live record stamped at or below the last recovery
      // record's time (OB::data_handler); the snapshot can share a nanosecond
      // with the first live message.
      if (tx <= live_floor_)
      {
        tx = live_floor_ + 1;
        ++stats_.ts_clamped;
      }
      else
        live_floor_ = 0;
    }
    last_tx_ = tx;
    return tx;
  }

  bfile::l3_mbo_v2_t mbo(uint8_t action, uint16_t loc, uint64_t ref, const OwnOrder &o,
                         uint64_t tx, bool eoe, bool recovery)
  {
    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = tx;
    r.sendingTime = tx;
    r.handlerendtim = recv_ns_;
    r.orderUpdateAction = action;
    r.securityID = ins_.asset(loc);
    r.orderID = ref;
    r.priority = o.prio;
    r.pxd = double(o.px) / 10000.0;
    r.displayQty = o.shares;
    r.side = o.side == 'B' ? '0' : '1';
    r.endOfEvent = eoe;
    r.recovery = recovery;
    r.pkt_entry_idx = pkt_idx_++;
    r.pkt_seq_num = pkt_seq_;
    return r;
  }

  bfile::l3_mbo_trd_v2_t mbot(uint64_t ref, uint32_t qty, uint64_t tx)
  {
    bfile::l3_mbo_trd_v2_t r{};
    r.typ = en::l3::MBOT_V2;
    r.venue = cfg_.venue;
    r.recv_time = recv_ns_;
    r.transactTime = tx;
    r.sendingTime = tx;
    r.handlerendtim = recv_ns_;
    r.lastQty = int32_t(qty);
    r.orderID = ref;
    r.pkt_entry_idx = pkt_idx_++;
    r.pkt_seq_num = pkt_seq_;
    return r;
  }

  bfile::l3_mbp_trd_t print(uint16_t loc, uint32_t px, int32_t sz, uint8_t aggressor,
                            uint64_t tx, bool eoe)
  {
    bfile::l3_mbp_trd_t r{};
    r.typ = en::l3::MBPT;
    r.venue = cfg_.venue;
    r.transactTime = tx;
    r.sendingTime = tx;
    r.handlerendtim = recv_ns_;
    r.securityID = ins_.asset(loc);
    r.pxd = double(px) / 10000.0;
    r.aggresorSide = aggressor;
    r.sz = sz;
    r.endOfEvent = eoe;
    return r;
  }

  void update_status(uint16_t loc)
  {
    auto &i = ins_.at(loc);
    i.status = derive_status(i);
    if (i.asset < 0 || i.status == i.sent_status)
      return;
    i.sent_status = i.status;
    bfile::l3_sst_t s{};
    s.typ = en::l3::SST;
    s.venue = cfg_.venue;
    s.txtim = last_tx_;
    s.sendtim = last_tx_;
    s.securityID = i.asset;
    s.tradingStatus = i.status;
    send(i.asset, s);
    touch(loc);
  }

  void update_all_status()
  {
    for (uint32_t loc = 1; loc < 65536; ++loc)
      if (ins_.at(uint16_t(loc)).known)
        update_status(uint16_t(loc));
  }

  // Own-book cross check: when the Timestamp advances, every security changed
  // since the previous one is checked while its status is 17.
  void cross_boundary(uint64_t ts)
  {
    if (ts < last_ts_)
      ++stats_.ts_backwards;
    if (ts <= last_ts_)
      return;
    last_ts_ = ts;
    for (auto loc : xcheck_)
    {
      xcheck_flag_[loc] = 0;
      if (ins_.at(loc).status != 17)
        continue;
      const auto b = book_.best_bid(loc), a = book_.best_ask(loc);
      if (b && a && b > a)
      {
        if (stats_.crossed_17++ < 20)
          logf("crossed while status 17: " + ins_.at(loc).symbol + " bid " +
               std::to_string(b) + " ask " + std::to_string(a) + " seq " + std::to_string(seq_));
      }
      else if (b && a && b == a)
        ++stats_.locked_17;
    }
    xcheck_.clear();
  }

  void changed(uint16_t loc)
  {
    if (!xcheck_flag_[loc])
    {
      xcheck_flag_[loc] = 1;
      xcheck_.push_back(loc);
    }
  }

  void boundary_check(bool known_ref, bool is_add)
  {
    if (!boundary_seq_ || seq_ != boundary_seq_)
      return;
    if (is_add ? known_ref : !known_ref)
    {
      ++stats_.boundary_suspect;
      logf("GLIMPSE boundary: message " + std::to_string(seq_) +
           (is_add ? " adds an order the spin already had"
                   : " refers to an order the spin did not have") +
           ": N may be in the snapshot (decision 1)");
    }
  }

  void add_order(uint16_t loc, uint64_t ref, char side, uint32_t shares, uint32_t px,
                 uint64_t ts, bool recovery, bool eoe = true, const uint64_t *tx_in = nullptr)
  {
    if (px == 0)
    {
      ++stats_.price_zero;
      logf("Add with price 0, not sent: ref " + std::to_string(ref));
      return;
    }
    OwnOrder o{loc, side, px, shares, ++prio_, false};
    // An order whose price is not on the asset's tick cannot be placed in OB
    // (it would round onto another price). Sub-penny prices exist only below
    // $1 (Rule 612): on a penny-tick asset these are stub quotes far from the
    // touch. Such an order is kept in the own book and not sent; nothing that
    // later refers to it is sent either (feed.md section 10, known deviation).
    const bool routed = ins_.asset(loc) >= 0;
    if (routed && px % ins_.at(loc).tick)
    {
      if (stats_.off_tick++ < 10)
        logf("price " + std::to_string(px) + " not on the tick of " + ins_.at(loc).symbol +
             ": order kept, not sent");
    }
    else
      o.sent = routed;
    if (!book_.add(ref, o))
    {
      ++stats_.add_duplicate;
      return;
    }
    changed(loc);
    if (!o.sent)
      return;
    const uint64_t tx = tx_in ? *tx_in : stamp(ts, recovery);
    send(ins_.asset(loc), mbo(0, loc, ref, o, tx, eoe, recovery));
    touch(loc);
  }

  // E and C: print (when printable), order-level execution, book change.
  void execute(uint16_t loc, uint64_t ref, uint32_t qty, uint64_t ts, bool printable,
               uint32_t print_px_or_0, char type)
  {
    OwnOrder *o = book_.find(ref);
    boundary_check(o != nullptr, false);
    if (!o)
    {
      ++stats_.unknown_ref[uint8_t(type)];
      return;
    }
    if (qty > o->shares)
      ++stats_.exec_over;
    const OwnOrder before = *o;
    const uint16_t oloc = before.locate;
    const uint32_t left = book_.reduce(ref, qty);
    changed(oloc);
    if (!before.sent)
      return;
    const uint64_t tx = stamp(ts, false);
    const int32_t a = ins_.asset(oloc);
    if (printable)
    {
      // Decision 5: aggressor opposite to the resting order (2 sell if the
      // order is a buy, 1 buy if a sell).
      const uint8_t aggr = before.side == 'B' ? 2 : 1;
      send(a, print(oloc, print_px_or_0 ? print_px_or_0 : before.px, int32_t(qty), aggr, tx, false));
    }
    send(a, mbot(ref, qty, tx));
    OwnOrder after = before;
    after.shares = left;
    send(a, mbo(left ? 1 : 2, oloc, ref, after, tx, true, false));
    touch(oloc);
    (void)loc;
  }

  template <class M>
  static uint16_t locate_of(const M &m)
  {
    if constexpr (requires { m.StockLocate; })
      return uint16_t(m.StockLocate);
    else if constexpr (requires { m.LocateCode; })
      return uint16_t(m.LocateCode);
    else
      return 0;
  }

  template <class M>
  void on(const M &m, bool recovery)
  {
    constexpr char T = M::type;
    if (!recovery)
      ++stats_.msgs[uint8_t(T) & 127];
    const uint16_t loc = locate_of(m);
    const uint64_t ts = uint64_t(m.Timestamp);
    if (!recovery && cfg_.check_cross)
      cross_boundary(ts);

    if constexpr (T == 'S')
    {
      // System Event (ITCH p.5).
      if (recovery)
        return;   // a spin's System Event is history; the live stream sets the phase
      const char ev = m.EventCode;
      stamp(ts, false);
      if (ev == 'S')
      {
        phase_ = 1;
        update_all_status();
      }
      else if (ev == 'E')
      {
        phase_ = 2;
        update_all_status();
      }
      else if (ev == 'C')
      {
        reset(false);   // End of Messages: the day is over (feed.md section 7)
      }
    }
    else if constexpr (T == 'R')
    {
      ins_.on_directory(loc, m.get_Stock(), uint32_t(m.RoundLotSize), m.MarketCategory,
                        m.Authenticity);
      if (!recovery)
        stamp(ts, false);
      update_status(loc);
    }
    else if constexpr (T == 'H')
    {
      ins_.at(loc).trading_state = m.TradingState;
      if (!recovery)
        stamp(ts, false);
      update_status(loc);
    }
    else if constexpr (T == 'h')
    {
      if (m.MarketCode == cfg_.market_code)
      {
        ins_.at(loc).op_halt = m.OperationalHaltAction == 'H';
        if (!recovery)
          stamp(ts, false);
        update_status(loc);
      }
    }
    else if constexpr (T == 'A' || T == 'F')
    {
      const uint64_t ref = m.OrderReferenceNumber;
      if (!recovery)
        boundary_check(book_.find(ref) != nullptr, true);
      add_order(loc, ref, m.BuySellIndicator, uint32_t(m.Shares), uint32_t(m.Price), ts, recovery);
    }
    else
    {
      if constexpr (T == 'E')
        execute(loc, m.OrderReferenceNumber, uint32_t(m.ExecutedShares), ts, true, 0, T);
      else if constexpr (T == 'C')
        execute(loc, m.OrderReferenceNumber, uint32_t(m.ExecutedShares), ts,
                m.Printable == 'Y', uint32_t(m.ExecutionPrice), T);
      else if constexpr (T == 'X')
      {
        const uint64_t ref = m.OrderReferenceNumber;
        OwnOrder *o = book_.find(ref);
        boundary_check(o != nullptr, false);
        if (!o)
        {
          ++stats_.unknown_ref[uint8_t(T)];
          return;
        }
        const uint32_t n = m.CancelledShares;
        if (n > o->shares)
          ++stats_.exec_over;
        OwnOrder after = *o;
        after.shares = book_.reduce(ref, n);
        changed(after.locate);
        if (!after.sent)
          return;
        // Same price, smaller size: OB keeps the place (feed.md section 7).
        send(ins_.asset(after.locate),
             mbo(after.shares ? 1 : 2, after.locate, ref, after, stamp(ts, false), true, false));
        touch(after.locate);
      }
      else if constexpr (T == 'D')
      {
        const uint64_t ref = m.OrderReferenceNumber;
        OwnOrder *o = book_.find(ref);
        boundary_check(o != nullptr, false);
        if (!o)
        {
          ++stats_.unknown_ref[uint8_t(T)];
          return;
        }
        const OwnOrder before = *o;
        book_.erase(ref);
        changed(before.locate);
        if (!before.sent)
          return;
        send(ins_.asset(before.locate), mbo(2, before.locate, ref, before, stamp(ts, false), true, false));
        touch(before.locate);
      }
      else if constexpr (T == 'U')
      {
        // Decision 3: delete the original, add the new reference at the back
        // of its level; same transactTime, endOfEvent only on the add.
        const uint64_t oref = m.OriginalOrderReferenceNumber, nref = m.NewOrderReferenceNumber;
        OwnOrder *o = book_.find(oref);
        boundary_check(o != nullptr, false);
        if (!o)
        {
          ++stats_.unknown_ref[uint8_t(T)];
          return;
        }
        const OwnOrder before = *o;
        book_.erase(oref);
        changed(before.locate);
        const bool routed = before.sent;
        uint64_t tx = 0;
        if (routed)
        {
          tx = stamp(ts, false);
          send(ins_.asset(before.locate), mbo(2, before.locate, oref, before, tx, false, false));
          touch(before.locate);
        }
        add_order(before.locate, nref, before.side, uint32_t(m.Shares), uint32_t(m.Price), ts, false,
                  true, routed ? &tx : nullptr);
      }
      else if constexpr (T == 'P')
      {
        // Non-displayed match: a print, no book change; side carries nothing
        // (always B), aggressor 0 (decision 5).
        if (ins_.asset(loc) >= 0)
        {
          send(ins_.asset(loc), print(loc, uint32_t(m.Price), int32_t(uint32_t(m.Shares)), 0,
                                      stamp(ts, false), true));
          touch(loc);
        }
      }
      else if constexpr (T == 'Q')
      {
        // Decision 4: one print per Q with Shares > 0; C-N made none.
        uint64_t sh = m.Shares;
        if (sh > 0 && ins_.asset(loc) >= 0)
        {
          if (sh > uint64_t(std::numeric_limits<int32_t>::max()))
          {
            ++stats_.cross_capped;
            logf("Cross Trade shares capped: " + std::to_string(sh));
            sh = uint64_t(std::numeric_limits<int32_t>::max());
          }
          send(ins_.asset(loc), print(loc, uint32_t(m.CrossPrice), int32_t(sh), 0, stamp(ts, false), true));
          touch(loc);
        }
      }
      else if constexpr (T == 'B')
      {
        ++stats_.broken;
        logf("Broken Trade, match " + std::to_string(uint64_t(m.MatchNumber)) +
             ": print stays in the volume (known deviation)");
      }
      // Y L V W K J I N O: no record (feed.md section 7).
    }
  }
};
} // namespace nasdaq_md
