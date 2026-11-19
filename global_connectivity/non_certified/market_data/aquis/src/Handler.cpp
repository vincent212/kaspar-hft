/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "aquis/Handler.hpp"

#include <algorithm>
#include <cstring>

#include "aquis/gen/aquis_md.hpp"
#include "enum/e_names.hpp"

namespace aquis
{

namespace mdf = aquis::md::from_aquis;
namespace snf = aquis::snap::from_aquis;

static constexpr size_t HDR = 6;  // msgType, length, seqNo (MD p.11, 3.3.1)
static constexpr double PX = 1e5; // 5 implied decimals (MD p.10, 3.1)

static uint32_t le32(const char *p)
{
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v; // little endian on the wire (MD p.10, 3.1); hosts are little endian
}

static char kside(uint8_t aquis_side) { return aquis_side == 1 ? '0' : '1'; }

static std::string str(std::string_view s) { return std::string(s); }

Handler::Handler(const HandlerConfig &cfg, Instruments &ins, Emit emit, RequestReplay request)
    : cfg_(cfg), ins_(ins), emit_(std::move(emit)), request_(std::move(request))
{
  if (!cfg_.start_of_day)
    state_ = State::snapshot;
}

uint8_t Handler::kaspar_status(uint8_t trading_status, uint8_t market_flags)
{
  switch (trading_status)
  {
  case 2:
    return 2;  // Halted -> TradingHalt
  case 3:
    return 18; // Suspended -> NotAvailableForTrading
  case 1:      // Active
  case 8:      // AoD Start: the continuous book trades on
  case 24:     // AoD Finish
    return (market_flags & 1) ? 17 : 4; // bit 0: continuous trading open
  default:
    return 18;
  }
}

// ---------------------------------------------------------------- packets

void Handler::on_packet(const char *p, size_t n)
{
  ++stats_.packets;
  pkt_has_seq_ = false;
  pkt_send_ = 0;
  if (n < 1)
  {
    ++stats_.malformed_packets;
    return;
  }
  const unsigned count = uint8_t(p[0]);
  size_t off = 1;
  unsigned seen = 0;
  for (; seen < count; ++seen)
  {
    if (off + HDR > n)
      break;
    const size_t len = uint8_t(p[off + 1]);
    if (len < HDR || off + len > n)
      break;
    const uint8_t type = uint8_t(p[off]);
    const uint32_t seq = le32(p + off + 2);
    ++stats_.messages;
    if (!pkt_has_seq_)
      pkt_seq_ = seq, pkt_has_seq_ = true;
    const bool unsequenced = type == mdf::Heartbeat::type ||
        (!cfg_.bundles_sequenced && (type == mdf::MDBundleStart::type || type == mdf::MDBundleEnd::type));
    if (type == mdf::Heartbeat::type)
      heartbeat(seq);
    else if (unsequenced)
    {
      if (state_ == State::live)
        apply(p + off, len, seq);
    }
    else
      message(p + off, len, seq, false);
    off += len;
  }
  if (seen < count)
    ++stats_.malformed_packets;
  else if (off != n)
    ++stats_.count_mismatch;

  bfile::l3_eop_t eop{};
  eop.typ = en::l3::EOP;
  eop.venue = cfg_.venue;
  eop.msgSeqNum = pkt_seq_;
  eop.sendingTime = pkt_send_;
  emit_(RECORDER, eop);
}

void Handler::heartbeat(uint32_t seq)
{
  ++stats_.heartbeats;
  // seqNo is the next business message expected (MD p.11, 3.3.2).
  if (seq == 0)
    return;
  highest_seen_ = std::max(highest_seen_, seq - 1);
  if (state_ == State::live && seq > expected_)
    open_gap(expected_, seq - 1);
}

// ---------------------------------------------------------------- sequencing

void Handler::message(const char *p, size_t len, uint32_t seq, bool replayed)
{
  if (!replayed)
    highest_seen_ = std::max(highest_seen_, seq);
  if (seq < expected_)
  {
    ++stats_.duplicates; // the other feed of the A/B pair, or a replay overlap
    return;
  }
  if (state_ == State::live)
  {
    if (seq == expected_)
    {
      apply(p, len, seq);
      ++expected_;
      return;
    }
    open_gap(expected_, seq - 1);
  }
  if (!buffer_.emplace(seq, std::string(p, len)).second)
    ++stats_.duplicates;
  if (state_ == State::replay)
    drain();
}

void Handler::open_gap(uint32_t first, uint32_t last)
{
  ++stats_.gaps;
  bfile::l3_gap_v2_t g{};
  g.typ = en::l3::GAP_V2;
  g.venue = cfg_.venue;
  emit_(ALL_BOOKS, g);
  if (cfg_.use_replay && request_ && last - first < cfg_.max_replay)
  {
    state_ = State::replay;
    requested_to_ = last;
    ++stats_.replay_requests;
    request_(first, last);
  }
  else
    go_snapshot();
}

// Apply buffered messages in sequence. A hole after the replayed range (a new
// gap while replaying) gets its own request; with nothing left to wait for,
// the feed is live again.
void Handler::drain()
{
  while (!buffer_.empty())
  {
    auto it = buffer_.begin();
    if (it->first < expected_)
    {
      buffer_.erase(it);
      continue;
    }
    if (it->first != expected_)
      break;
    apply(it->second.data(), it->second.size(), it->first);
    ++expected_;
    buffer_.erase(it);
  }
  if (state_ != State::replay)
    return;
  if (buffer_.empty())
  {
    if (expected_ > highest_seen_)
      state_ = State::live;
    return;
  }
  const uint32_t next = buffer_.begin()->first;
  if (expected_ > requested_to_)
  {
    // The requested range is in; what is missing now is a later gap.
    if (cfg_.use_replay && next - 1 - expected_ < cfg_.max_replay)
    {
      requested_to_ = next - 1;
      ++stats_.replay_requests;
      request_(expected_, next - 1);
    }
    else
      go_snapshot();
  }
}

void Handler::go_snapshot()
{
  state_ = State::snapshot;
  snap_ = Snap{};
  pending_.clear();
  in_bundle_ = false;
  orders_.clear();
  bfile::l3_chr_v2_t c{};
  c.typ = en::l3::CHR_V2;
  c.venue = cfg_.venue;
  emit_(ALL_BOOKS, c);
}

void Handler::on_replay_lost()
{
  replay_buf_.clear();
  if (state_ == State::replay)
    go_snapshot();
}

void Handler::on_replay_data(const char *p, size_t n)
{
  replay_buf_.append(p, n);
  size_t off = 0;
  while (replay_buf_.size() - off >= HDR)
  {
    const char *m = replay_buf_.data() + off;
    const size_t len = uint8_t(m[1]);
    if (len < HDR)
    {
      on_replay_lost(); // the stream cannot be followed
      return;
    }
    if (replay_buf_.size() - off < len)
      break;
    const uint8_t type = uint8_t(m[0]);
    if (type == 15) // Replay Response (MD p.21, 3.8.3)
    {
      const auto d = aquis::replay::from_aquis::decode(m, len);
      if (d)
      {
        const auto &r = std::get<aquis::replay::from_aquis::ReplayResponse>(d.msg);
        if (r.responseCode != 0)
        {
          ++stats_.replay_refused;
          if (state_ == State::replay)
            go_snapshot();
        }
      }
    }
    else if (type != mdf::Heartbeat::type)
    {
      const bool unsequenced = !cfg_.bundles_sequenced &&
          (type == mdf::MDBundleStart::type || type == mdf::MDBundleEnd::type);
      if (!unsequenced && state_ == State::replay)
        message(m, len, le32(m + 2), true);
    }
    off += len;
  }
  replay_buf_.erase(0, off);
}

// ---------------------------------------------------------------- snapshot

void Handler::on_snapshot_packet(const char *p, size_t n)
{
  if (state_ != State::snapshot || n < 1)
    return;
  const unsigned count = uint8_t(p[0]);
  size_t off = 1;
  for (unsigned i = 0; i < count && off + HDR <= n; ++i)
  {
    const size_t len = uint8_t(p[off + 1]);
    if (len < HDR || off + len > n)
    {
      snap_.active = false; // a broken packet breaks the snapshot
      return;
    }
    snapshot_message(p + off, len);
    if (state_ != State::snapshot)
      return;
    off += len;
  }
}

void Handler::snapshot_message(const char *p, size_t len)
{
  const auto d = snf::decode(p, len);
  if (!d)
    return;
  auto discard = [this] {
    if (snap_.active)
      ++stats_.snapshots_discarded;
    snap_.active = false;
  };
  if (auto *s = std::get_if<snf::SnapshotStart>(&d.msg))
  {
    if (snap_.active)
      ++stats_.snapshots_discarded; // the previous one did not complete
    snap_ = Snap{};
    snap_.active = true;
    snap_.stream_seq = s->streamSeqNo;
    snap_.count = s->securityCount;
    snap_.ts = s->timestamp;
  }
  else if (auto *b = std::get_if<snf::BookStatus>(&d.msg))
  {
    if (!snap_.active)
      return;
    if (!snap_.books.empty() && snap_.books.back().remaining)
      return discard();
    SnapBook book;
    book.id = b->securityID;
    book.status = b->tradingStatus;
    book.flags = b->marketFlags;
    book.remaining = b->entries;
    snap_.books.push_back(book);
  }
  else if (auto *e = std::get_if<snf::BookEntry>(&d.msg))
  {
    if (!snap_.active)
      return;
    if (snap_.books.empty() || snap_.books.back().id != e->securityID || !snap_.books.back().remaining)
      return discard();
    auto &book = snap_.books.back();
    book.entries.push_back({e->Side, e->quantity, e->Price, e->orderRef});
    --book.remaining;
  }
  else
    return; // MaC Book Entry: not used (feed.md section 7)

  if (snap_.active && snap_.books.size() == snap_.count &&
      (snap_.books.empty() || !snap_.books.back().remaining))
    apply_snapshot();
}

// The snapshot holds the book after streamSeqNo (MD p.19, 3.7.1; review.md
// decision 3). Buffered messages up to streamSeqNo are already in it; the rest
// are applied after it, with a replay request for any hole before them.
void Handler::apply_snapshot()
{
  ++stats_.snapshots_applied;
  const Snap s = std::move(snap_);
  snap_ = Snap{};
  orders_.clear();
  const uint64_t tx = stamp(s.ts);
  for (const auto &b : s.books)
  {
    const int32_t sec = ins_.sec_id(b.id);
    if (sec < 0)
      continue;
    bfile::l3_sst_t st{};
    st.typ = en::l3::SST;
    st.venue = cfg_.venue;
    st.txtim = tx;
    st.sendtim = tx;
    st.securityID = sec;
    st.tradingStatus = kaspar_status(b.status, b.flags);
    push(sec, st);
    uint64_t prio = 0;
    for (const auto &e : b.entries)
    {
      Order o{sec, kside(e.side), e.price, e.qty};
      orders_[e.ref] = o;
      push_mbo(sec, 0, e.ref, o, tx, tx, ++prio, true);
    }
    end_event();
  }
  last_tx_ = std::max(last_tx_, tx);
  expected_ = s.stream_seq + 1;
  highest_seen_ = std::max(highest_seen_, s.stream_seq);
  state_ = State::replay;
  requested_to_ = s.stream_seq;
  drain();
  if (state_ == State::replay && buffer_.empty() && expected_ > highest_seen_)
    state_ = State::live;
}

// ---------------------------------------------------------------- dispatch

uint64_t Handler::stamp(uint64_t ts)
{
  // One transactTime per event, never going back (OB treats records sharing a
  // transactTime as one transaction: feed.md section 7, event boundaries).
  if (in_bundle_)
  {
    if (!bundle_tx_)
      bundle_tx_ = std::max(ts, last_tx_);
    return bundle_tx_;
  }
  return std::max(ts, last_tx_);
}

void Handler::end_event()
{
  if (pending_.empty())
    return;
  auto &last = pending_.back().second;
  if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&last))
    m->endOfEvent = true;
  else if (auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&last))
    x->endOfEvent = true;
  else if (auto *t = std::get_if<bfile::l3_mbp_trd_t>(&last))
    t->endOfEvent = true;
  for (const auto &[sec, r] : pending_)
  {
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&r))
      last_tx_ = std::max(last_tx_, m->transactTime);
    else if (auto *t = std::get_if<bfile::l3_mbp_trd_t>(&r))
      last_tx_ = std::max(last_tx_, t->transactTime);
    else if (auto *s = std::get_if<bfile::l3_sst_t>(&r))
      last_tx_ = std::max(last_tx_, s->txtim);
    emit_(sec, r);
  }
  pending_.clear();
}

void Handler::push_mbo(int32_t sec, uint8_t action, uint64_t oid, const Order &o, uint64_t tx,
                       uint64_t st, uint64_t prio, bool recovery)
{
  bfile::l3_mbo_v2_t r{};
  r.typ = en::l3::MBO_V2;
  r.venue = cfg_.venue;
  r.transactTime = tx;
  r.sendingTime = st;
  r.orderUpdateAction = action;
  r.securityID = sec;
  r.orderID = oid;
  r.priority = prio;
  r.pxd = double(o.price) / PX;
  r.displayQty = o.qty;
  r.side = o.side;
  r.recovery = recovery;
  r.pkt_seq_num = pkt_seq_;
  push(sec, r);
}

void Handler::apply(const char *p, size_t len, uint32_t seq)
{
  const auto d = mdf::decode(p, len);
  if (d.st == status::unknown)
  {
    ++stats_.unknown; // a type this table does not list; its length skips it
    return;
  }
  if (d.st != status::ok)
  {
    ++stats_.short_messages; // length shorter than the table's message
    return;
  }
  auto send_time = [this](uint64_t pub, uint64_t tx) {
    pkt_send_ = pub ? pub : tx;
    return pub ? pub : tx;
  };

  if (std::holds_alternative<mdf::MDBundleStart>(d.msg))
  {
    end_event();
    in_bundle_ = true;
    bundle_tx_ = 0;
    return;
  }
  if (std::holds_alternative<mdf::MDBundleEnd>(d.msg))
  {
    end_event();
    in_bundle_ = false;
    bundle_tx_ = 0;
    return;
  }

  if (auto *m = std::get_if<mdf::OrderAdd>(&d.msg))
  {
    const int32_t sec = ins_.sec_id(m->securityID);
    if (sec < 0)
    {
      ++stats_.not_routed;
      return;
    }
    const uint64_t tx = stamp(m->timestamp), st = send_time(m->pubTimestamp, tx);
    if (auto it = orders_.find(m->orderRef); it != orders_.end())
    {
      ++stats_.unknown_orders; // orderRef reused while live: replace it
      push_mbo(it->second.sec, 2, m->orderRef, it->second, tx, st, seq, false);
      orders_.erase(it);
    }
    Order o{sec, kside(m->side), m->price, m->quantity};
    orders_[m->orderRef] = o;
    push_mbo(sec, 0, m->orderRef, o, tx, st, seq, false);
  }
  else if (auto *m = std::get_if<mdf::OrderCancel>(&d.msg))
  {
    auto it = orders_.find(m->orderRef);
    if (it == orders_.end())
    {
      if (ins_.sec_id(m->securityID) >= 0)
        ++stats_.unknown_orders;
      return;
    }
    const uint64_t tx = stamp(m->timestamp), st = send_time(m->pubTimestamp, tx);
    push_mbo(it->second.sec, 2, m->orderRef, it->second, tx, st, seq, false);
    orders_.erase(it);
  }
  else if (auto *m = std::get_if<mdf::OrderModify>(&d.msg))
  {
    auto it = orders_.find(m->orderRef);
    if (it == orders_.end())
    {
      if (ins_.sec_id(m->securityID) >= 0)
        ++stats_.unknown_orders;
      return;
    }
    const uint64_t tx = stamp(m->timestamp), st = send_time(m->pubTimestamp, tx);
    Order &o = it->second;
    if (m->quantity == 0)
    {
      push_mbo(o.sec, 2, m->orderRef, o, tx, st, seq, false);
      orders_.erase(it);
    }
    else if (m->price == o.price && m->quantity == o.qty)
    {
      // "on revision of quantity down the order retains its position in the
      // book, otherwise the book is re-ordered" (MD p.13, 3.4.5): a modify that
      // keeps price and size loses priority, where OB would keep it. Delete
      // then add, same orderID (review.md decision 4).
      ++stats_.same_size_modifies;
      push_mbo(o.sec, 2, m->orderRef, o, tx, st, seq, false);
      push_mbo(o.sec, 0, m->orderRef, o, tx, st, seq, false);
    }
    else
    {
      // Size down at the same price keeps priority; a new price or a larger
      // size loses it: OB's own rule.
      o.price = m->price;
      o.qty = m->quantity;
      push_mbo(o.sec, 1, m->orderRef, o, tx, st, seq, false);
    }
  }
  else if (auto *m = std::get_if<mdf::Trade>(&d.msg))
  {
    const int32_t sec = ins_.sec_id(m->securityID);
    if (sec < 0)
    {
      ++stats_.not_routed;
      return;
    }
    const uint64_t tx = stamp(m->timestamp), st = send_time(m->pubTimestamp, tx);
    auto it = m->tradeType == 1 && m->orderRef ? orders_.find(m->orderRef) : orders_.end();
    if (m->tradeType != 1 && m->orderRef && orders_.count(m->orderRef))
      ++stats_.orderref_on_other_trade; // review.md decision 5
    if (m->tradeType == 1 && it == orders_.end())
      ++stats_.unknown_orders;

    bfile::l3_mbp_trd_t t{};
    t.typ = en::l3::MBPT;
    t.venue = cfg_.venue;
    t.transactTime = tx;
    t.sendingTime = st;
    t.securityID = sec;
    t.pxd = double(m->price) / PX;
    t.sz = int32_t(m->quantity);
    t.numorders = 1;
    t.lastTrade = true;
    if (it != orders_.end())
    {
      Order &o = it->second;
      t.side = o.side;
      t.aggresorSide = o.side == '0' ? 2 : 1; // the aggressor took the other side
      push(sec, t);

      uint32_t q = m->quantity;
      if (q > o.qty)
      {
        ++stats_.overfills;
        q = o.qty;
      }
      bfile::l3_mbo_trd_v2_t x{};
      x.typ = en::l3::MBOT_V2;
      x.venue = cfg_.venue;
      x.transactTime = tx;
      x.sendingTime = st;
      x.lastQty = int32_t(q);
      x.orderID = m->orderRef;
      x.lastTrade = true;
      x.pkt_seq_num = pkt_seq_;
      push(o.sec, x);

      o.qty -= q;
      if (o.qty == 0)
      {
        o.qty = 0;
        push_mbo(o.sec, 2, m->orderRef, o, tx, st, seq, false);
        orders_.erase(it);
      }
      else
        push_mbo(o.sec, 1, m->orderRef, o, tx, st, seq, false);
    }
    else
    {
      if (m->tradeType == 2)
        ++stats_.hidden_trades;
      push(sec, t); // print only: hidden, MaC, cross, AoD, AVM
    }
  }
  else if (std::holds_alternative<mdf::TradeBust>(d.msg))
  {
    ++stats_.busts; // the records cannot withdraw a print (feed.md section 10)
    return;
  }
  else if (auto *m = std::get_if<mdf::SecurityStatus>(&d.msg))
  {
    const int32_t sec = ins_.sec_id(m->securityID);
    if (sec < 0)
    {
      ++stats_.not_routed;
      return;
    }
    const uint64_t tx = stamp(m->timestamp), st = send_time(m->pubTimestamp, tx);
    bfile::l3_sst_t s{};
    s.typ = en::l3::SST;
    s.venue = cfg_.venue;
    s.txtim = tx;
    s.sendtim = st;
    s.securityID = sec;
    s.tradingStatus = kaspar_status(m->tradingStatus, m->marketFlags);
    push(sec, s);
  }
  else if (auto *m = std::get_if<mdf::SecurityDefinition>(&d.msg))
  {
    ins_.on_definition(m->securityID, str(m->get_UMTF()), str(m->get_ISIN()), str(m->get_currency()),
                       str(m->get_MIC()), m->tickTableId, m->flags, str(m->get_exchangeSymbol()));
    return;
  }
  else if (auto *m = std::get_if<mdf::TickTableData>(&d.msg))
  {
    ins_.on_tick_band(m->tickTableID, str(m->get_name()), m->threshold, m->tickSize);
    return;
  }
  else
    return; // AoD Update, MaC Update: no record (feed.md section 7)

  if (!in_bundle_)
    end_event();
}

// ---------------------------------------------------------------- requests

std::string encode_login(const std::string &user, const std::string &password)
{
  aquis::replay::to_aquis::Login l{};
  l.length = sizeof(l);
  l.seqNo = 0;
  l.set_username(user);
  l.set_password(password);
  char buf[sizeof(l)];
  const size_t n = aquis::replay::to_aquis::encode(buf, sizeof(buf), l);
  return std::string(buf, n);
}

std::string encode_replay_request(uint32_t begin, uint32_t end)
{
  aquis::replay::to_aquis::ReplayRequest r{};
  r.length = sizeof(r);
  r.seqNo = 0;
  r.beginSeqNo = begin;
  r.endSeqNo = end;
  char buf[sizeof(r)];
  const size_t n = aquis::replay::to_aquis::encode(buf, sizeof(buf), r);
  return std::string(buf, n);
}

} // namespace aquis
