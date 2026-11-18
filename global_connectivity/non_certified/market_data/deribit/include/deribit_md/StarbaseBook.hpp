#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// StarbaseBook: Deribit Starbase order-by-order books for every instrument on
// the channels it is fed, synchronised as Deribit specifies, emitting Kaspar
// L3 records for the instruments routed to an OB. NOT CERTIFIED. No network:
// the multicast reader (or the PCAP tool, or a test) hands it each decoded
// packet.
//
// Synchronisation (feed.md 5, "Maintaining the order book"):
//   - incremental packets carry a per-channel sequence; message i of a packet
//     has seqNum + i; a heartbeat carries the next number. A higher number than
//     expected is a gap; a lower one a duplicate (dropped).
//   - until an instrument is synchronised, its incremental messages are
//     buffered. Its snapshot (SnapshotHeader .. Puts .. SnapshotTrailer on the
//     snapshot channel) gives incrementalSeqNum, the last incremental already
//     in it; the buffered messages after it are applied, the rest dropped.
//   - after a gap, every instrument on the channel is unsynchronised (a gap
//     record is sent to each routed book) until a snapshot whose
//     incrementalSeqNum is at or past the last missing number. The Retransmit
//     Gateway is not used: it is reachable only from LD4 or AWS PrivateLink.
//
// Queue priority is explicit: each order carries sortOrderId, and each price
// level is kept sorted by it (smaller first). The mapping to Kaspar records,
// which OB orders by arrival, is in feed.md 7:
//   new order (Put of an unknown orderId)      add (action 0)
//   Put of a known order, same sortOrderId     change (action 1)
//   Put of a known order, new sortOrderId      delete then add, same orderID
//   QtyReduced                                 change (action 1), priority kept
//   Delete                                     delete (action 2)
//   Trade (31) with a maker order              l3_mbo_trd_v2_t before that
//                                              order's book change in the same
//                                              transaction, and the print
//                                              l3_mbp_trd_t (aggressor side from
//                                              TradeSummary.takerFlags.isSell)
//   InstrumentStatusUpdate / status in a       l3_sst_t
//   definition
//   synchronisation                            l3_chr_v2_t, then every order as
//                                              a recovery add in priority order
// Records of one Starbase transaction (start/endOfTransaction flags) are
// released together, endOfEvent on the last one of each instrument, then
// l3_eop_t (packet sequence, low 32 bits) and l3_eob_t when no transaction is
// open at the end of a packet.
//
// Everything the specification promises is also checked, and counted in
// Stats: no crossed book, every reduce/delete/fill refers to a known order,
// a new order joins the back of its level, a reduction reduces, quantities on
// the instrument's step, fills consistent with the maker's next quantity, and
// the rebuilt book equal to each later snapshot taken at a point it can be
// compared at.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bfile/r_l3.hpp"
#include "deribit_md/Starbase.hpp"

namespace deribit::starbase
{
  struct Order
  {
    int64_t id = 0;
    bool bid = true;
    int64_t price9 = 0, qty = 0, sortOrderId = 0;
  };

  struct Instrument
  {
    int64_t id = 0;
    InstrumentDefinition def;
    bool have_def = false;
    int32_t channel = 0;
    int8_t status = 0;
    bool synced = false;
    int64_t synced_seq = 0;   // incrementalSeqNum of the snapshot it last synced from
    int64_t last_seq = 0;     // last incremental message applied to it
    std::unordered_map<int64_t, Order> orders;
    // price9 -> (sortOrderId -> orderId), per side
    std::map<int64_t, std::map<int64_t, int64_t>> bids, asks;
    int32_t sec_id = 0;       // Kaspar securityID; 0 = not routed to an OB
    uint64_t last_tx = 0;     // last transactTime sent to its OB (kept increasing)
    uint64_t changed = 0;     // incremental messages applied since the last snapshot check

    // (price9, total quantity, orders) per level, best first.
    struct Level
    {
      int64_t price9, qty;
      size_t orders;
    };
    std::vector<Level> levels(bool bid) const;
  };

  class StarbaseBook
  {
  public:
    // Kaspar securityID for an instrument, or 0 to keep its book without
    // routing it (options, combos, prices that can be zero or negative).
    using Route = std::function<int32_t(const InstrumentDefinition &)>;
    using Emit = std::function<void(int32_t sec_id, const bfile::l3_t &)>;

    struct Config
    {
      char venue = 0;   // en::x
      Route route;
      Emit emit;
      size_t max_buffer = 1 << 20;   // incremental messages held for unsynced instruments
    };

    explicit StarbaseBook(Config cfg);

    void on_packet(const PacketHeader &ph, const std::vector<Message> &msgs);

    const Instrument *instrument(int64_t id) const;
    const std::unordered_map<int64_t, Instrument> &instruments() const { return inst_; }
    // The instrument's price in its own ticks (pxd of every record); 0 if unknown.
    int64_t ticks(const Instrument &in, int64_t price9) const;
    uint32_t units(const Instrument &in, int64_t qty) const;

    struct Stats
    {
      uint64_t packets = 0, messages = 0, heartbeats = 0;
      uint64_t inc_messages = 0, snap_messages = 0, applied = 0, buffered = 0;
      uint64_t dropped_before_sync = 0;   // buffered, at or below the snapshot's seq
      uint64_t duplicates = 0, gaps = 0, missing = 0, snap_gaps = 0;
      uint64_t syncs = 0, resyncs = 0;
      uint64_t snapshots = 0, cycles = 0;
      uint64_t snap_compared_changed = 0;   // compared after incremental changes to the book
      uint64_t snap_compared = 0, snap_match = 0, snap_mismatch = 0, snap_not_comparable = 0;
      uint64_t snap_trailer_mismatch = 0;
      uint64_t snap_priority_disorder = 0;   // a snapshot level not in sortOrderId order
      uint64_t crossed = 0;                  // transactions after which a synced, open book is crossed
      uint64_t unknown_ref = 0;              // reduce, delete or fill of an order not on the book
      uint64_t put_new = 0, put_smaller = 0, put_same = 0, put_larger_same_sort = 0;
      uint64_t put_reprice_same_sort = 0, put_new_sort = 0, put_side_change = 0;
      uint64_t joined_ahead = 0;             // a new order whose sortOrderId is not the level's largest
      uint64_t reduce_not_smaller = 0;
      uint64_t qty_off_step = 0, px_off_tick = 0, px_not_positive = 0;
      uint64_t trades = 0, trade_summaries = 0, block_trades = 0, implied_fills = 0;
      uint64_t fill_px_mismatch = 0, fill_consistent = 0, fill_inconsistent = 0;
      uint64_t txn_unclosed = 0;             // a start flag while a transaction was open
      uint64_t records = 0;
    };
    const Stats &stats() const { return st_; }

  private:
    struct Channel
    {
      int64_t next_seq = -1;   // next incremental expected; -1 before the first
      int64_t join_seq = -1;   // first seq held since joining or the last gap
      int64_t gap_to = -1;     // a resync needs a snapshot at or past this seq
      std::deque<Message> buffer;
      int64_t snap_next = -1;
      struct Pending
      {
        SnapshotBound hdr;
        std::vector<Put> puts;
        bool broken = false;
        bool active = false;
      } pending;
    };

    struct TxnRecord
    {
      int32_t sec_id;
      bfile::l3_t r;
      int64_t order_id;    // the order this book change is about; 0 otherwise
    };

    Config cfg_;
    std::unordered_map<int64_t, Instrument> inst_;
    std::unordered_map<int32_t, Channel> ch_;
    Stats st_;

    // The open transaction.
    bool txn_open_ = false;
    std::vector<TxnRecord> txn_;
    bool txn_sell_ = false;   // aggressor side from the last TradeSummary
    std::unordered_map<int64_t, int64_t> txn_fill_rem_;   // maker -> expected remaining qty
    std::unordered_map<int64_t, int64_t> txn_pre_qty_;    // maker -> qty before this transaction's change
    std::unordered_set<int64_t> txn_insts_;               // instruments the transaction touched

    Instrument &get(int64_t id);
    void incremental(const PacketHeader &ph, const std::vector<Message> &msgs);
    void snapshot(const PacketHeader &ph, const std::vector<Message> &msgs);
    void on_gap(int32_t channel, Channel &c, int64_t from, int64_t to);
    void complete_snapshot(int32_t channel, Channel &c);
    void sync(Instrument &in, const Channel::Pending &p, Channel &c);
    void dispatch(const Message &m, Channel &c);
    void apply(Instrument &in, const Message &m);
    void end_transaction();

    // Book edits.
    void add(Instrument &in, const Order &o);
    void remove(Instrument &in, const Order &o);

    // Kaspar records.
    uint64_t stamp(Instrument &in, int64_t tx);
    void push(Instrument &in, const bfile::l3_t &r, int64_t order_id = 0);
    bfile::l3_mbo_v2_t mbo(Instrument &in, uint8_t action, const Order &o, uint64_t tx, bool recovery);
    void status(Instrument &in, int8_t s, int64_t tx);
    bool routed(const Instrument &in) const { return in.sec_id != 0 && in.synced; }
  };

  // Starbase InstrumentStatus to CME SecurityTradingStatus (feed.md 7).
  uint8_t trading_status(int8_t s);
}
