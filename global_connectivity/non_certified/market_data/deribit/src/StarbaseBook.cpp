/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_md/StarbaseBook.hpp"

#include <algorithm>
#include <iostream>
#include <set>

#include "enum/e_names.hpp"

namespace deribit::starbase
{

uint8_t trading_status(int8_t s)
{
  switch (s)
  {
  case 0: return 17;   // OPEN -> ReadyToTrade
  case 1: return 18;   // INACTIVE -> NotAvailableForTrading
  case 2: return 2;    // SETTLEMENT -> TradingHalt (review.md: settlement)
  case 3: return 4;    // DELIVERED -> Close
  case 4: return 2;    // LOCKED -> TradingHalt
  case 5: return 2;    // HALTED -> TradingHalt
  }
  return 18;
}

namespace
{
  int64_t instrument_of(const Body &b)
  {
    return std::visit(
        [](const auto &x) -> int64_t {
          if constexpr (requires { x.instrumentId; })
            return x.instrumentId;
          else
            return 0;
        },
        b);
  }

  bool book_message(const Body &b)
  {
    return std::holds_alternative<Put>(b) || std::holds_alternative<QtyReduced>(b) ||
           std::holds_alternative<Delete>(b) || std::holds_alternative<TradeSummary>(b) ||
           std::holds_alternative<Trade>(b) || std::holds_alternative<BlockTrade>(b) ||
           std::holds_alternative<InstrumentStatusUpdate>(b) ||
           std::holds_alternative<InstrumentDefinition>(b);
  }

  void set_tx(bfile::l3_t &r, uint64_t tx)
  {
    std::visit(
        [tx](auto &x) {
          if constexpr (requires { x.transactTime; })
            x.transactTime = tx;
          if constexpr (requires { x.sendingTime; })
            x.sendingTime = tx;
          if constexpr (requires { x.txtim; })
            x.txtim = tx;
          if constexpr (requires { x.sendtim; })
            x.sendtim = tx;
        },
        r);
  }

  uint64_t get_tx(const bfile::l3_t &r)
  {
    return std::visit(
        [](const auto &x) -> uint64_t {
          if constexpr (requires { x.transactTime; })
            return x.transactTime;
          else if constexpr (requires { x.txtim; })
            return x.txtim;
          else
            return 0;
        },
        r);
  }

  void set_end(bfile::l3_t &r)
  {
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&r))
      m->endOfEvent = true;
    else if (auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&r))
      x->endOfEvent = true;
    else if (auto *p = std::get_if<bfile::l3_mbp_trd_t>(&r))
      p->endOfEvent = true;
  }
}

std::vector<Instrument::Level> Instrument::levels(bool bid) const
{
  std::vector<Level> v;
  auto one = [&](const auto &lvl) {
    Level l{lvl.first, 0, lvl.second.size()};
    for (const auto &[so, id] : lvl.second)
      l.qty += orders.at(id).qty;
    v.push_back(l);
  };
  if (bid)
    for (auto it = bids.rbegin(); it != bids.rend(); ++it)
      one(*it);
  else
    for (const auto &l : asks)
      one(l);
  return v;
}

StarbaseBook::StarbaseBook(Config cfg) : cfg_(std::move(cfg)) {}

const Instrument *StarbaseBook::instrument(int64_t id) const
{
  auto it = inst_.find(id);
  return it == inst_.end() ? nullptr : &it->second;
}

Instrument &StarbaseBook::get(int64_t id)
{
  auto &in = inst_[id];
  in.id = id;
  return in;
}

int64_t StarbaseBook::ticks(const Instrument &in, int64_t price9) const
{
  if (!in.have_def || in.def.tickSize9 <= 0)
    return 0;
  return price9 / in.def.tickSize9;
}

uint32_t StarbaseBook::units(const Instrument &in, int64_t qty) const
{
  const int64_t u = in.have_def && in.def.minOrderQuantity > 0 ? in.def.minOrderQuantity : 1;
  return uint32_t((qty + u - 1) / u);
}

// ---- packets --------------------------------------------------------------------------

void StarbaseBook::on_packet(const PacketHeader &ph, const std::vector<Message> &msgs)
{
  ++st_.packets;
  st_.messages += msgs.size();
  if (ph.type & SNAPSHOT)
    snapshot(ph, msgs);
  else if (ph.type & (INCREMENTAL | RETRANSMIT))
    incremental(ph, msgs);
}

void StarbaseBook::on_gap(int32_t channel, Channel &c, int64_t from, int64_t to)
{
  ++st_.gaps;
  st_.missing += uint64_t(to - from + 1);
  std::cerr << "StarbaseBook: channel " << channel << " missing " << from << ".." << to << std::endl;
  c.gap_to = to;
  c.join_seq = to + 1;
  c.buffer.clear();
  for (auto &[id, in] : inst_)
  {
    if (in.channel != channel || !in.synced)
      continue;
    if (in.sec_id)
    {
      bfile::l3_gap_v2_t g{};
      g.typ = en::l3::GAP_V2;
      g.venue = cfg_.venue;
      cfg_.emit(in.sec_id, g);
      ++st_.records;
    }
    in.synced = false;
  }
}

void StarbaseBook::incremental(const PacketHeader &ph, const std::vector<Message> &msgs)
{
  Channel &c = ch_[ph.channelId];
  if (msgs.empty())
  {
    ++st_.heartbeats;
    if (c.next_seq < 0)
      c.next_seq = c.join_seq = ph.seqNum;
    else if (ph.seqNum > c.next_seq)
    {
      on_gap(ph.channelId, c, c.next_seq, ph.seqNum - 1);
      c.next_seq = ph.seqNum;
    }
    return;
  }
  std::set<int32_t> touched;
  for (const auto &m : msgs)
  {
    ++st_.inc_messages;
    if (c.next_seq < 0)
      c.next_seq = c.join_seq = m.seq;
    if (m.seq < c.next_seq)
    {
      ++st_.duplicates;
      continue;
    }
    if (m.seq > c.next_seq)
      on_gap(ph.channelId, c, c.next_seq, m.seq - 1);
    c.next_seq = m.seq + 1;
    if (const int64_t id = instrument_of(m.body); id)
      if (auto it = inst_.find(id); it != inst_.end() && it->second.sec_id)
        touched.insert(it->second.sec_id);
    dispatch(m, c);
  }
  if (!txn_open_)
    for (const int32_t sec : touched)
    {
      bfile::l3_eop_t e{};
      e.typ = en::l3::EOP;
      e.venue = cfg_.venue;
      e.msgSeqNum = uint32_t(uint64_t(ph.seqNum + ph.messageCount - 1));
      e.sendingTime = uint64_t(ph.sendingTime);
      cfg_.emit(sec, e);
      bfile::l3_eob_t b{};
      b.typ = en::l3::EOB;
      b.venue = cfg_.venue;
      cfg_.emit(sec, b);
      st_.records += 2;
    }
}

void StarbaseBook::dispatch(const Message &m, Channel &c)
{
  if (!book_message(m.body))
    return;
  const int64_t id = instrument_of(m.body);
  Instrument &in = get(id);
  if (in.synced)
  {
    if (m.seq <= in.synced_seq)
    {
      ++st_.dropped_before_sync;
      return;
    }
    apply(in, m);
    return;
  }
  // Not synchronised: hold it for the instrument's snapshot. Definitions are
  // applied at once (they carry no book state).
  if (auto d = std::get_if<InstrumentDefinition>(&m.body))
  {
    in.def = *d;
    in.have_def = true;
  }
  c.buffer.push_back(m);
  ++st_.buffered;
  while (c.buffer.size() > cfg_.max_buffer)
  {
    c.join_seq = c.buffer.front().seq + 1;
    c.buffer.pop_front();
  }
}

void StarbaseBook::snapshot(const PacketHeader &ph, const std::vector<Message> &msgs)
{
  Channel &c = ch_[ph.channelId];
  for (const auto &m : msgs)
  {
    ++st_.snap_messages;
    if (c.snap_next >= 0 && m.seq != c.snap_next)
    {
      ++st_.snap_gaps;
      c.pending.broken = true;   // the instrument being sent lost messages
    }
    c.snap_next = m.seq + 1;

    if (auto b = std::get_if<SnapshotBound>(&m.body))
    {
      if (b->header)
      {
        c.pending = {};
        c.pending.hdr = *b;
        c.pending.active = true;
      }
      else if (c.pending.active && b->instrumentId == c.pending.hdr.instrumentId)
      {
        if (b->incrementalSeqNum != c.pending.hdr.incrementalSeqNum)
          ++st_.snap_trailer_mismatch;
        else if (!c.pending.broken)
          complete_snapshot(ph.channelId, c);
        c.pending.active = false;
      }
    }
    else if (auto d = std::get_if<InstrumentDefinition>(&m.body))
    {
      Instrument &in = get(d->instrumentId);
      in.def = *d;
      in.have_def = true;
      in.channel = ph.channelId;
      if (!in.sec_id && cfg_.route)
        in.sec_id = cfg_.route(in.def);
    }
    else if (auto p = std::get_if<Put>(&m.body))
    {
      if (c.pending.active && p->instrumentId == c.pending.hdr.instrumentId)
        c.pending.puts.push_back(*p);
    }
    else if (std::holds_alternative<EndOfCycle>(m.body))
      ++st_.cycles;
  }
}

void StarbaseBook::complete_snapshot(int32_t channel, Channel &c)
{
  ++st_.snapshots;
  const auto &p = c.pending;
  Instrument &in = get(p.hdr.instrumentId);
  in.channel = channel;

  // The snapshot sends each level in execution priority (feed.md 5).
  std::map<std::pair<bool, int64_t>, int64_t> last_sort;
  for (const auto &put : p.puts)
  {
    auto key = std::make_pair(put.bid, put.price9);
    auto it = last_sort.find(key);
    if (it != last_sort.end() && put.sortOrderId <= it->second)
      ++st_.snap_priority_disorder;
    last_sort[key] = put.sortOrderId;
  }

  const int64_t S = p.hdr.incrementalSeqNum;
  if (in.synced)
  {
    // The rebuilt book can be compared with the snapshot when nothing for this
    // instrument has been applied past S, and the channel has reached S.
    if (in.last_seq <= S && c.next_seq - 1 >= S)
    {
      ++st_.snap_compared;
      if (in.changed)
        ++st_.snap_compared_changed;
      in.changed = 0;
      bool eq = in.orders.size() == p.puts.size();
      for (const auto &put : p.puts)
      {
        auto it = in.orders.find(put.orderId);
        if (it == in.orders.end() || it->second.bid != put.bid || it->second.price9 != put.price9 ||
            it->second.qty != put.qty || it->second.sortOrderId != put.sortOrderId)
          eq = false;
      }
      if (eq)
        ++st_.snap_match;
      else
      {
        ++st_.snap_mismatch;
        std::cerr << "StarbaseBook: instrument " << in.id << " (" << in.def.name
                  << ") differs from its snapshot at seq " << S << ": " << in.orders.size()
                  << " orders rebuilt, " << p.puts.size() << " in the snapshot" << std::endl;
      }
    }
    else
      ++st_.snap_not_comparable;
    return;
  }

  // Not synchronised: usable if we hold every incremental after S.
  if (c.next_seq < 0 || c.join_seq > S + 1 || S < c.gap_to)
    return;
  sync(in, p, c);
}

void StarbaseBook::sync(Instrument &in, const Channel::Pending &p, Channel &c)
{
  const bool again = in.sec_id && in.last_tx;
  in.orders.clear();
  in.bids.clear();
  in.asks.clear();
  for (const auto &put : p.puts)
  {
    Order o{put.orderId, put.bid, put.price9, put.qty, put.sortOrderId};
    add(in, o);
  }
  in.synced = true;
  in.synced_seq = p.hdr.incrementalSeqNum;
  in.last_seq = p.hdr.incrementalSeqNum;
  in.status = in.def.status;
  ++st_.syncs;
  if (again)
    ++st_.resyncs;

  if (in.sec_id)
  {
    const uint64_t tx = stamp(in, p.hdr.incrementalTimestamp);
    bfile::l3_chr_v2_t chr{};
    chr.typ = en::l3::CHR_V2;
    chr.venue = cfg_.venue;
    cfg_.emit(in.sec_id, chr);
    bfile::l3_sst_t s{};
    s.typ = en::l3::SST;
    s.venue = cfg_.venue;
    s.txtim = s.sendtim = tx;
    s.securityID = in.sec_id;
    s.tradingStatus = trading_status(in.status);
    cfg_.emit(in.sec_id, s);
    st_.records += 2;
    // Every order as a recovery add, each level in priority order.
    std::vector<bfile::l3_mbo_v2_t> recs;
    for (const auto *side : {&in.bids, &in.asks})
      for (const auto &[px, q] : *side)
        for (const auto &[so, id] : q)
          recs.push_back(mbo(in, 0, in.orders.at(id), tx, true));
    for (size_t i = 0; i < recs.size(); ++i)
    {
      recs[i].endOfEvent = i + 1 == recs.size();
      cfg_.emit(in.sec_id, recs[i]);
    }
    st_.records += recs.size();
  }

  // The buffered messages after the snapshot, in order; the rest are in it.
  std::deque<Message> keep;
  for (auto &m : c.buffer)
  {
    if (instrument_of(m.body) != in.id)
    {
      keep.push_back(std::move(m));
      continue;
    }
    if (m.seq <= in.synced_seq)
      ++st_.dropped_before_sync;
    else
      apply(in, m);
  }
  c.buffer.swap(keep);
  if (txn_open_)
    end_transaction();
}

// ---- applying one incremental message --------------------------------------------------

void StarbaseBook::add(Instrument &in, const Order &o)
{
  in.orders[o.id] = o;
  (o.bid ? in.bids : in.asks)[o.price9][o.sortOrderId] = o.id;
}

void StarbaseBook::remove(Instrument &in, const Order &o)
{
  auto &side = o.bid ? in.bids : in.asks;
  auto it = side.find(o.price9);
  if (it != side.end())
  {
    it->second.erase(o.sortOrderId);
    if (it->second.empty())
      side.erase(it);
  }
  in.orders.erase(o.id);
}

uint64_t StarbaseBook::stamp(Instrument &in, int64_t tx)
{
  const uint64_t t = std::max(uint64_t(tx), in.last_tx + 1);
  in.last_tx = t;
  return t;
}

bfile::l3_mbo_v2_t StarbaseBook::mbo(Instrument &in, uint8_t action, const Order &o, uint64_t tx,
                                     bool recovery)
{
  bfile::l3_mbo_v2_t r{};
  r.typ = en::l3::MBO_V2;
  r.venue = cfg_.venue;
  r.transactTime = r.sendingTime = tx;
  r.orderUpdateAction = action;
  r.securityID = in.sec_id;
  r.orderID = uint64_t(o.id);
  r.priority = uint64_t(o.sortOrderId);
  r.pxd = double(ticks(in, o.price9));
  r.displayQty = units(in, o.qty);
  r.side = o.bid ? '0' : '1';
  r.recovery = recovery;
  return r;
}

void StarbaseBook::push(Instrument &in, const bfile::l3_t &r, int64_t order_id)
{
  txn_.push_back({in.sec_id, r, order_id});
}

void StarbaseBook::status(Instrument &in, int8_t s, int64_t tx)
{
  in.status = s;
  if (!routed(in))
    return;
  bfile::l3_sst_t r{};
  r.typ = en::l3::SST;
  r.venue = cfg_.venue;
  r.txtim = r.sendtim = uint64_t(tx);
  r.securityID = in.sec_id;
  r.tradingStatus = trading_status(s);
  push(in, r);
}

void StarbaseBook::apply(Instrument &in, const Message &m)
{
  if (m.start() && txn_open_)
  {
    ++st_.txn_unclosed;
    end_transaction();
  }
  txn_open_ = true;
  txn_insts_.insert(in.id);
  ++st_.applied;
  in.last_seq = m.seq;
  ++in.changed;
  const uint64_t tx = uint64_t(m.transactTime);
  const bool r = routed(in);

  auto check_px = [&](int64_t price9) {
    if (in.have_def && in.def.tickSize9 > 0 && price9 % in.def.tickSize9 != 0)
      ++st_.px_off_tick;
    if (price9 <= 0)
      ++st_.px_not_positive;
  };
  auto check_qty = [&](int64_t q) {
    if (in.have_def && in.def.minOrderQuantity > 0 && q % in.def.minOrderQuantity != 0)
      ++st_.qty_off_step;
  };
  // The quantity an order had before this transaction first changed it (for
  // the fill check at the end of the transaction).
  auto note_pre = [&](const Order &o) { txn_pre_qty_.try_emplace(o.id, o.qty); };
  auto routable = [&](int64_t price9) { return r && price9 > 0; };

  if (auto p = std::get_if<Put>(&m.body))
  {
    check_px(p->price9);
    check_qty(p->qty);
    Order o{p->orderId, p->bid, p->price9, p->qty, p->sortOrderId};
    auto it = in.orders.find(p->orderId);
    if (it == in.orders.end())
    {
      ++st_.put_new;
      const auto &side = p->bid ? in.bids : in.asks;
      if (auto lvl = side.find(p->price9); lvl != side.end() && !lvl->second.empty() &&
                                           lvl->second.rbegin()->first >= p->sortOrderId)
        ++st_.joined_ahead;
      add(in, o);
      if (routable(o.price9))
        push(in, mbo(in, 0, o, tx, false), o.id);
    }
    else
    {
      const Order old = it->second;
      note_pre(old);
      if (old.bid != o.bid)
        ++st_.put_side_change;
      const bool same_sort = old.sortOrderId == o.sortOrderId;
      if (!same_sort)
        ++st_.put_new_sort;
      else if (old.price9 != o.price9)
        ++st_.put_reprice_same_sort;
      else if (o.qty < old.qty)
        ++st_.put_smaller;
      else if (o.qty == old.qty)
        ++st_.put_same;
      else
        ++st_.put_larger_same_sort;
      remove(in, old);
      add(in, o);
      if (routable(o.price9))
      {
        if (same_sort && old.bid == o.bid)
          push(in, mbo(in, 1, o, tx, false), o.id);
        else
        {
          // Priority reset where OB would keep it: delete, then add with the
          // same orderID and transactTime; endOfEvent falls on the add.
          push(in, mbo(in, 2, old, tx, false), o.id);
          push(in, mbo(in, 0, o, tx, false), o.id);
        }
      }
    }
  }
  else if (auto q = std::get_if<QtyReduced>(&m.body))
  {
    check_qty(q->qty);
    auto it = in.orders.find(q->orderId);
    if (it == in.orders.end())
      ++st_.unknown_ref;
    else
    {
      note_pre(it->second);
      if (q->qty >= it->second.qty)
        ++st_.reduce_not_smaller;
      it->second.qty = q->qty;
      if (routable(it->second.price9))
        push(in, mbo(in, 1, it->second, tx, false), q->orderId);
    }
  }
  else if (auto d = std::get_if<Delete>(&m.body))
  {
    auto it = in.orders.find(d->orderId);
    if (it == in.orders.end())
      ++st_.unknown_ref;
    else
    {
      const Order old = it->second;
      note_pre(old);
      remove(in, old);
      if (routable(old.price9))
        push(in, mbo(in, 2, old, tx, false), d->orderId);
    }
  }
  else if (auto s = std::get_if<TradeSummary>(&m.body))
  {
    ++st_.trade_summaries;
    txn_sell_ = s->takerFlags & 1;
  }
  else if (auto t = std::get_if<Trade>(&m.body))
  {
    ++st_.trades;
    check_px(t->price9);
    const bool implied = t->makerOrderId == kNullInt64;
    if (implied)
      ++st_.implied_fills;
    bool known = false;
    if (!implied)
    {
      auto it = in.orders.find(t->makerOrderId);
      // A maker deleted earlier in this transaction is known through txn_pre_qty_.
      known = it != in.orders.end() || txn_pre_qty_.count(t->makerOrderId);
      if (!known)
        ++st_.unknown_ref;
      else
      {
        if (it != in.orders.end())
        {
          note_pre(it->second);
          if (it->second.price9 != t->price9)
            ++st_.fill_px_mismatch;
        }
        txn_fill_rem_[t->makerOrderId] += t->qty;
      }
    }
    if (r)
    {
      if (known)
      {
        // The execution goes before the maker's book change in this
        // transaction, wherever Starbase put the Trade (feed.md 7).
        bfile::l3_mbo_trd_v2_t x{};
        x.typ = en::l3::MBOT_V2;
        x.venue = cfg_.venue;
        x.transactTime = x.sendingTime = tx;
        x.lastQty = int32_t(units(in, t->qty));
        x.orderID = uint64_t(t->makerOrderId);
        auto pos = std::find_if(txn_.begin(), txn_.end(), [&](const TxnRecord &rec) {
          return rec.order_id == t->makerOrderId;
        });
        txn_.insert(pos, TxnRecord{in.sec_id, x, 0});
      }
      bfile::l3_mbp_trd_t pr{};
      pr.typ = en::l3::MBPT;
      pr.venue = cfg_.venue;
      pr.transactTime = pr.sendingTime = tx;
      pr.securityID = in.sec_id;
      pr.pxd = double(ticks(in, t->price9));
      pr.sz = int32_t(units(in, t->qty));
      pr.aggresorSide = txn_sell_ ? 2 : 1;   // CME AggressorSide: 1 buy, 2 sell
      pr.side = txn_sell_ ? '1' : '0';
      pr.numorders = 1;
      push(in, pr);
    }
  }
  else if (std::holds_alternative<BlockTrade>(m.body))
    ++st_.block_trades;   // off-book: no record (review.md)
  else if (auto u = std::get_if<InstrumentStatusUpdate>(&m.body))
    status(in, u->tradingStatus, m.transactTime);
  else if (auto def = std::get_if<InstrumentDefinition>(&m.body))
  {
    const bool changed = def->status != in.status;
    in.def = *def;
    in.have_def = true;
    if (changed)
      status(in, def->status, m.transactTime);
  }

  if (m.end())
    end_transaction();
}

void StarbaseBook::end_transaction()
{
  // Fills against makers: the quantity before, less what traded, must be what
  // the maker has now (0 when deleted).
  for (const auto &[id, filled] : txn_fill_rem_)
  {
    auto pre = txn_pre_qty_.find(id);
    if (pre == txn_pre_qty_.end())
      continue;
    int64_t now = 0;
    for (const auto &[iid, in] : inst_)
      if (auto it = in.orders.find(id); it != in.orders.end())
      {
        now = it->second.qty;
        break;
      }
    if (pre->second - filled == now)
      ++st_.fill_consistent;
    else
      ++st_.fill_inconsistent;
  }
  txn_fill_rem_.clear();
  txn_pre_qty_.clear();
  txn_open_ = false;

  // One stamp per instrument per transaction; endOfEvent on its last record.
  std::unordered_map<int32_t, size_t> last;
  std::unordered_map<int32_t, uint64_t> tx;
  // endOfEvent goes on the instrument's last book record (OB does not read
  // prints), or on its last record when the transaction has only prints.
  std::unordered_map<int32_t, size_t> last_book;
  for (size_t i = 0; i < txn_.size(); ++i)
  {
    last[txn_[i].sec_id] = i;
    if (!std::holds_alternative<bfile::l3_mbp_trd_t>(txn_[i].r) &&
        !std::holds_alternative<bfile::l3_sst_t>(txn_[i].r))
      last_book[txn_[i].sec_id] = i;
    auto &t = tx[txn_[i].sec_id];
    t = std::max(t, get_tx(txn_[i].r));
  }
  for (const int64_t id : txn_insts_)
  {
    Instrument &in = inst_.at(id);
    if (auto t = tx.find(in.sec_id); in.sec_id && t != tx.end())
      t->second = stamp(in, int64_t(t->second));
  }
  for (size_t i = 0; i < txn_.size(); ++i)
  {
    auto &rec = txn_[i];
    set_tx(rec.r, tx[rec.sec_id]);
    const auto lb = last_book.find(rec.sec_id);
    if ((lb != last_book.end() ? lb->second : last[rec.sec_id]) == i)
      set_end(rec.r);
    cfg_.emit(rec.sec_id, rec.r);
    ++st_.records;
  }
  txn_.clear();

  // No synchronised, open book may be crossed after a transaction.
  for (const int64_t id : txn_insts_)
  {
    const Instrument &in = inst_.at(id);
    if (in.synced && in.status == 0 && !in.bids.empty() && !in.asks.empty() &&
        in.bids.rbegin()->first >= in.asks.begin()->first)
    {
      ++st_.crossed;
      std::cerr << "StarbaseBook: " << in.def.name << " crossed: bid " << in.bids.rbegin()->first
                << " ask " << in.asks.begin()->first << std::endl;
    }
  }
  txn_insts_.clear();
}

}
