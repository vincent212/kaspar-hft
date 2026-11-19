/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange/CoinbaseExchangeSync.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

#include "enum/e_names.hpp"

namespace coinbase_exchange
{

// ---- ProductSpec ------------------------------------------------------------------------

std::optional<ProductSpec> ProductSpec::make(const std::string &product, int32_t sec_id, char venue,
                                             const std::string &quote_increment,
                                             const std::string &base_increment, const std::string &qty_unit,
                                             std::string &err)
{
  ProductSpec p;
  p.product = product;
  p.sec_id = sec_id;
  p.venue = venue;
  p.price_decimals = decimals_of(quote_increment);
  p.size_decimals = decimals_of(base_increment);
  if (p.price_decimals < 0 || p.size_decimals < 0)
  {
    err = "bad quote_increment or base_increment";
    return std::nullopt;
  }
  auto inc = scaled(quote_increment, p.price_decimals);
  if (!inc || *inc <= 0)
  {
    err = "quote_increment must be positive";
    return std::nullopt;
  }
  p.price_increment = *inc;
  auto unit = scaled(qty_unit, p.size_decimals);
  auto base = scaled(base_increment, p.size_decimals);
  if (!unit || *unit <= 0 || !base || *base <= 0 || *unit % *base != 0)
  {
    err = "qty_unit must be a positive multiple of base_increment";
    return std::nullopt;
  }
  p.qty_unit = *unit;
  return p;
}

std::optional<int64_t> ProductSpec::ticks(std::string_view px) const
{
  auto v = scaled(px, price_decimals);
  if (!v || *v % price_increment != 0)
    return std::nullopt;
  return *v / price_increment;
}

uint32_t ProductSpec::display(int64_t size, bool *saturated) const
{
  if (size <= 0)
    return 0;
  int64_t d = (size + qty_unit - 1) / qty_unit;
  if (d > int64_t(kMaxDisplay))
  {
    if (saturated)
      *saturated = true;
    return kMaxDisplay;
  }
  return uint32_t(d);
}

// ---- construction and helpers -----------------------------------------------------------

CoinbaseExchangeSync::CoinbaseExchangeSync(Config cfg, Emit emit) : cfg_(std::move(cfg)), emit_(std::move(emit))
{
}

namespace
{
  const std::string *str(const nlohmann::json &m, const char *k)
  {
    auto it = m.find(k);
    if (it == m.end() || !it->is_string())
      return nullptr;
    return &it->get_ref<const std::string &>();
  }

  int64_t seq_of(const nlohmann::json &v)
  {
    if (v.is_number_integer() || v.is_number_unsigned())
      return v.get<int64_t>();
    if (v.is_number_float())
      return int64_t(v.get<double>());
    if (v.is_string())
    {
      try
      {
        return std::stoll(v.get<std::string>());
      }
      catch (...)
      {
      }
    }
    return -1;
  }
}

const CoinbaseExchangeSync::Order *CoinbaseExchangeSync::find(const Uuid &u) const
{
  auto it = orders_.find(u);
  return it == orders_.end() ? nullptr : &it->second;
}

int64_t CoinbaseExchangeSync::best(bool bid) const
{
  if (bid)
    return bids_.empty() ? 0 : bids_.rbegin()->first;
  return asks_.empty() ? 0 : asks_.begin()->first;
}

CoinbaseExchangeSync::Level CoinbaseExchangeSync::level(bool bid, int64_t px) const
{
  const auto &s = side(bid);
  auto it = s.find(px);
  return it == s.end() ? Level{} : it->second;
}

std::vector<Uuid> CoinbaseExchangeSync::queue(bool bid, int64_t px) const
{
  std::vector<std::pair<uint64_t, Uuid>> v;
  for (const auto &[u, o] : orders_)
    if (o.bid == bid && o.px == px)
      v.emplace_back(prio_.at(u), u);
  std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<Uuid> out;
  for (auto &p : v)
    out.push_back(p.second);
  return out;
}

// ---- parsing ----------------------------------------------------------------------------

bool CoinbaseExchangeSync::parse(const nlohmann::json &m, Event &ev)
{
  const auto &spec = cfg_.spec;
  auto set_type = [&](const std::string &t) {
    if (t == "received")
      ev.type = Event::Received;
    else if (t == "open")
      ev.type = Event::Open;
    else if (t == "done")
      ev.type = Event::Done;
    else if (t == "match" || t == "last_match")
      ev.type = Event::Match;
    else if (t == "change")
      ev.type = Event::Change;
    else if (t == "activate")
      ev.type = Event::Activate;
    else if (t == "noop")
      ev.type = Event::Noop;
    else
      return false;
    return true;
  };
  auto get_px = [&](const std::string *s) -> bool {
    if (!s)
      return true;
    auto t = spec.ticks(*s);
    if (!t)
      return false;
    ev.px = *t;
    ev.has_px = true;
    return true;
  };
  auto get_size = [&](const std::string *s) -> bool {
    if (!s)
      return true;
    auto v = spec.size(*s);
    if (!v)
      return false;
    ev.size = *v;
    ev.has_size = true;
    return true;
  };
  auto get_uuid = [](const std::string *s, Uuid &u) -> bool {
    if (!s)
      return true;
    auto p = parse_uuid(*s);
    if (!p)
      return false;
    u = *p;
    return true;
  };

  if (m.is_array())
  {
    // level3 channel: [type, product_id, sequence, ...] in the schema's order
    if (m.empty() || !m[0].is_string() || !set_type(m[0].get<std::string>()))
      return false;
    auto sch = l3_schema_.find(m[0].get<std::string>());
    if (sch == l3_schema_.end())
    {
      ++st_.bad_message;
      return false;
    }
    nlohmann::json obj = nlohmann::json::object();
    for (size_t i = 0; i < sch->size() && i < m.size(); ++i)
      obj[(*sch)[i].get<std::string>()] = m[i];
    if (str(obj, "product_id") == nullptr || *str(obj, "product_id") != spec.product)
      return false;
    ev.seq = obj.contains("sequence") ? seq_of(obj["sequence"]) : -1;
    if (auto t = str(obj, "time"))
      ev.time_ns = parse_iso_time(*t);
    if (auto s = str(obj, "side"))
      ev.side = *s == "buy" ? 'b' : *s == "sell" ? 's' : 0;
    bool ok = get_px(str(obj, "price")) && get_size(str(obj, "size")) &&
              get_uuid(str(obj, "order_id"), ev.order) && get_uuid(str(obj, "maker_order_id"), ev.maker) &&
              get_uuid(str(obj, "taker_order_id"), ev.taker);
    if (!ok)
    {
      ++st_.bad_message;
      return false;
    }
    return true;
  }

  if (!m.is_object())
    return false;
  const std::string *type = str(m, "type");
  if (!type || !set_type(*type))
    return false;
  const std::string *prod = str(m, "product_id");
  if (!prod || *prod != spec.product)
    return false;
  if (auto it = m.find("sequence"); it != m.end())
    ev.seq = seq_of(*it);
  if (auto t = str(m, "time"))
    ev.time_ns = parse_iso_time(*t);
  if (auto s = str(m, "side"))
    ev.side = *s == "buy" ? 'b' : *s == "sell" ? 's' : 0;
  if (auto r = str(m, "reason"))
    ev.reason = *r;
  bool ok = get_uuid(str(m, "order_id"), ev.order);
  switch (ev.type)
  {
  case Event::Open:
    ok = ok && get_px(str(m, "price")) && get_size(str(m, "remaining_size"));
    break;
  case Event::Done:
    ok = ok && get_px(str(m, "price")) && get_size(str(m, "remaining_size"));
    break;
  case Event::Match:
    ok = ok && get_px(str(m, "price")) && get_size(str(m, "size")) && get_uuid(str(m, "maker_order_id"), ev.maker) &&
         get_uuid(str(m, "taker_order_id"), ev.taker);
    break;
  case Event::Change:
    // modify_order: new_price/new_size; STP: price (null for market orders)/new_size
    ok = ok && get_size(str(m, "new_size"));
    if (str(m, "new_price"))
      ok = ok && get_px(str(m, "new_price"));
    else
      ok = ok && get_px(str(m, "price"));
    break;
  case Event::Received:
    ok = ok && get_px(str(m, "price")) && get_size(str(m, "size"));
    break;
  default:
    break;
  }
  if (!ok)
  {
    ++st_.bad_message;
    return false;
  }
  return true;
}

// ---- input --------------------------------------------------------------------------------

void CoinbaseExchangeSync::on_message(const nlohmann::json &m, uint64_t recv_ns)
{
  if (m.is_object())
  {
    const std::string *type = str(m, "type");
    if (!type)
      return;
    if (*type == "level3" && m.contains("schema"))
    {
      l3_schema_ = m["schema"];
      return;
    }
    if (*type == "heartbeat")
    {
      if (str(m, "product_id") && *str(m, "product_id") == cfg_.spec.product)
        on_heartbeat(m, recv_ns);
      return;
    }
    if (*type == "status")
    {
      if (auto it = m.find("products"); it != m.end() && it->is_array())
        for (const auto &p : *it)
          if (str(p, "id") && *str(p, "id") == cfg_.spec.product)
            on_product_status(p, recv_ns);
      return;
    }
  }
  Event ev;
  if (!parse(m, ev))
    return;
  ev.recv_ns = recv_ns;
  if (ev.seq < 0)
  {
    // activate carries no sequence and changes nothing on the book
    ++st_.other;
    return;
  }
  accept(std::move(ev));
}

// A heartbeat carries the product's sequence as the venue has it. One above
// the last applied means messages are missing here (held out of order, or
// lost). Missing for longer than heartbeat_gap_ns, with the stream not
// advancing past where it was when the first such heartbeat came, is a gap.
void CoinbaseExchangeSync::on_heartbeat(const nlohmann::json &m, uint64_t recv_ns)
{
  if (!synced_ || !cfg_.check_gaps)
    return;
  int64_t s = m.contains("sequence") ? seq_of(m["sequence"]) : -1;
  if (s <= last_seq_)
  {
    hb_ahead_since_ = 0;
    return;
  }
  ++st_.hb_ahead;
  if (hb_ahead_since_ == 0 || last_seq_ > hb_ahead_seq_)
  {
    hb_ahead_since_ = recv_ns;
    hb_ahead_seq_ = last_seq_;
    return;
  }
  if (recv_ns - hb_ahead_since_ > cfg_.heartbeat_gap_ns)
    gap("heartbeat ahead of the stream", recv_ns);
}

void CoinbaseExchangeSync::accept(Event &&ev)
{
  ++st_.messages;
  if (!synced_)
  {
    buffer_.push_back(std::move(ev));
    if (buffer_.size() > cfg_.max_buffer)
    {
      buffer_.pop_front();
      ++st_.buffered_dropped;
    }
    return;
  }
  if (ev.seq <= last_seq_)
  {
    ++st_.duplicates;
    return;
  }
  if (!cfg_.check_gaps || ev.seq == last_seq_ + 1)
  {
    last_seq_ = ev.seq;
    apply(ev);
    while (synced_ && !pending_.empty())
    {
      auto it = pending_.begin();
      if (it->first <= last_seq_)
      {
        ++st_.duplicates;
        pending_.erase(it);
        continue;
      }
      if (it->first != last_seq_ + 1)
        break;
      Event next = std::move(it->second);
      pending_.erase(it);
      last_seq_ = next.seq;
      ++st_.reordered;
      apply(next);
    }
    return;
  }
  // ahead of the next expected: hold it for a while
  if (!pending_.emplace(ev.seq, std::move(ev)).second)
  {
    ++st_.duplicates;
    return;
  }
  if (pending_.size() > cfg_.reorder_window)
    gap("sequence gap", pending_.rbegin()->second.recv_ns);
}

void CoinbaseExchangeSync::gap(const char *why, uint64_t now_ns)
{
  ++st_.gaps;
  std::cerr << "CoinbaseExchangeSync " << cfg_.spec.product << ": " << why << " after sequence " << last_seq_
            << "; resynchronising" << std::endl;
  bfile::l3_gap_v2_t g{};
  g.typ = en::l3::GAP_V2;
  g.venue = cfg_.spec.venue;
  emit_(g);
  synced_ = false;
  need_snapshot_ = true;
  hb_ahead_since_ = 0;
  for (auto &[s, e] : pending_)
    buffer_.push_back(std::move(e));
  pending_.clear();
  (void)now_ns;
}

void CoinbaseExchangeSync::on_disconnect(uint64_t now_ns)
{
  if (synced_)
  {
    ++st_.gaps;
    bfile::l3_gap_v2_t g{};
    g.typ = en::l3::GAP_V2;
    g.venue = cfg_.spec.venue;
    emit_(g);
  }
  synced_ = false;
  need_snapshot_ = true;
  hb_ahead_since_ = 0;
  pending_.clear();
  buffer_.clear();
  l3_schema_ = nlohmann::json();
  (void)now_ns;
}

CoinbaseExchangeSync::SnapResult CoinbaseExchangeSync::on_snapshot(const nlohmann::json &snap, uint64_t recv_ns)
{
  if (!snap.is_object() || !snap.contains("sequence") || !snap.contains("bids") || !snap.contains("asks"))
    return SnapResult::Bad;
  const int64_t S = seq_of(snap["sequence"]);
  if (S < 0)
    return SnapResult::Bad;
  const uint64_t snap_time = str(snap, "time") ? parse_iso_time(*str(snap, "time")) : recv_ns;

  // The buffered messages after the snapshot, in sequence order.
  for (auto &[s, e] : pending_)
    buffer_.push_back(std::move(e));
  pending_.clear();
  std::vector<Event> after;
  after.reserve(buffer_.size());
  for (auto &e : buffer_)
    if (e.seq > S)
      after.push_back(std::move(e));
  buffer_.clear();
  std::stable_sort(after.begin(), after.end(), [](const Event &a, const Event &b) { return a.seq < b.seq; });
  after.erase(std::unique(after.begin(), after.end(), [](const Event &a, const Event &b) { return a.seq == b.seq; }),
              after.end());
  if (cfg_.check_gaps)
  {
    int64_t expect = S + 1;
    for (const auto &e : after)
    {
      if (e.seq != expect)
      {
        // Messages between the snapshot and the buffer (or inside it) are
        // missing: this snapshot cannot be used. Keep what is buffered.
        ++st_.snapshots_too_old;
        for (auto &k : after)
          buffer_.push_back(std::move(k));
        return SnapResult::TooOld;
      }
      ++expect;
    }
  }

  // Parse the snapshot before touching the book.
  struct Row
  {
    Uuid u;
    int64_t px, size;
  };
  std::vector<Row> bids, asks;
  for (int side = 0; side < 2; ++side)
  {
    const auto &arr = snap[side == 0 ? "bids" : "asks"];
    if (!arr.is_array())
      return SnapResult::Bad;
    auto &out = side == 0 ? bids : asks;
    out.reserve(arr.size());
    for (const auto &r : arr)
    {
      if (!r.is_array() || r.size() < 3 || !r[0].is_string() || !r[1].is_string() || !r[2].is_string())
        return SnapResult::Bad;
      auto px = cfg_.spec.ticks(r[0].get_ref<const std::string &>());
      auto sz = cfg_.spec.size(r[1].get_ref<const std::string &>());
      auto u = parse_uuid(r[2].get_ref<const std::string &>());
      if (!px || !sz || !u)
        return SnapResult::Bad;
      out.push_back({*u, *px, *sz});
    }
  }

  ++st_.snapshots;
  if (started_)
    ++st_.resyncs;
  if (!started_)
  {
    started_ = true;
    if (!status_)
    {
      bool auction = snap.value("auction_mode", false);
      send_status(auction ? 21 : 17, snap_time);
    }
  }

  if (st_.snapshots > 1)
  {
    // A resync: tell OB the book is cleared before the recovery records. OB
    // clears on the first recovery record only when it is not already in a
    // recovery, which it still is if no live record came since the last one.
    bfile::l3_chr_v2_t c{};
    c.typ = en::l3::CHR_V2;
    c.venue = cfg_.spec.venue;
    emit_(c);
  }
  clear_book();
  Event rec;
  rec.time_ns = snap_time;
  rec.recv_ns = recv_ns;
  begin(rec);
  for (int side = 0; side < 2; ++side)
    for (const auto &r : side == 0 ? bids : asks)
    {
      Event a;
      a.type = Event::Open;
      a.order = r.u;
      a.side = side == 0 ? 'b' : 's';
      a.px = r.px;
      a.has_px = true;
      a.size = r.size;
      a.has_size = true;
      add(a, true, recv_ns, snap_time);
    }
  end_event(S, snap_time);

  synced_ = true;
  need_snapshot_ = false;
  hb_ahead_since_ = 0;
  last_seq_ = S;
  for (auto &e : after)
  {
    if (!synced_)
    {
      buffer_.push_back(std::move(e)); // a gap while applying: keep the rest
      continue;
    }
    last_seq_ = e.seq;
    apply(e);
  }
  return SnapResult::Applied;
}

// ---- trading status --------------------------------------------------------------------

uint8_t CoinbaseExchangeSync::trading_status(const nlohmann::json &p)
{
  auto s = p.value("status", std::string("online"));
  if (s != "online")
    return 4; // offline, delisted: closed
  if (p.value("trading_disabled", false) || p.value("cancel_only", false))
    return 2; // halted: nothing can trade
  if (p.value("auction_mode", false))
    return 21; // pre-open: an auction is collecting; the book may be crossed
  return 17;   // ready to trade (limit_only and post_only included)
}

void CoinbaseExchangeSync::on_product_status(const nlohmann::json &p, uint64_t now_ns)
{
  uint8_t s = trading_status(p);
  if (s != status_)
    send_status(s, now_ns);
}

void CoinbaseExchangeSync::send_status(uint8_t status, uint64_t now_ns)
{
  status_ = status;
  bfile::l3_sst_t r{};
  r.typ = en::l3::SST;
  r.venue = cfg_.spec.venue;
  event_tx_ = std::max(now_ns, last_tx_ + 1);
  last_tx_ = event_tx_;
  r.txtim = event_tx_;
  r.sendtim = event_tx_;
  r.securityID = cfg_.spec.sec_id;
  r.tradingStatus = status;
  emit_(r);
  bfile::l3_eob_t eob{};
  eob.typ = en::l3::EOB;
  eob.venue = cfg_.spec.venue;
  emit_(eob);
}

// ---- applying ---------------------------------------------------------------------------

// All records of one event share its transactTime; transactTime strictly
// increases from one event to the next (OB checks the book at each change of
// transactTime, and rejects data stamped at or before a recovery).
void CoinbaseExchangeSync::begin(const Event &ev)
{
  out_.clear();
  uint64_t tx = ev.time_ns ? ev.time_ns : ev.recv_ns;
  event_tx_ = std::max(tx, last_tx_ + 1);
}

void CoinbaseExchangeSync::end_event(int64_t seq, uint64_t send_time)
{
  if (out_.empty())
    return;
  for (auto it = out_.rbegin(); it != out_.rend(); ++it)
  {
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&*it))
    {
      m->endOfEvent = true;
      break;
    }
    if (auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&*it))
    {
      x->endOfEvent = true;
      break;
    }
  }
  last_tx_ = event_tx_;
  for (const auto &r : out_)
    emit_(r);
  out_.clear();
  bfile::l3_eop_t eop{};
  eop.typ = en::l3::EOP;
  eop.venue = cfg_.spec.venue;
  eop.msgSeqNum = uint32_t(uint64_t(seq) & 0xffffffffu);
  eop.sendingTime = send_time;
  emit_(eop);
  bfile::l3_eob_t eob{};
  eob.typ = en::l3::EOB;
  eob.venue = cfg_.spec.venue;
  emit_(eob);
}

uint64_t CoinbaseExchangeSync::make_oid(const Uuid &u)
{
  uint64_t id = order_id64(u);
  if (live_ids_.insert(id).second)
    return id;
  ++st_.id_collisions;
  for (;;)
  {
    id = (1ull << 63) | next_synth_++;
    if (live_ids_.insert(id).second)
      return id;
  }
}

void CoinbaseExchangeSync::free_oid(const Order &o)
{
  live_ids_.erase(o.oid);
}

void CoinbaseExchangeSync::level_add(const Order &o)
{
  auto &l = (o.bid ? bids_ : asks_)[o.px];
  l.size += o.size;
  l.disp += o.disp;
  l.count += 1;
}

void CoinbaseExchangeSync::level_sub(const Order &o)
{
  auto &s = o.bid ? bids_ : asks_;
  auto it = s.find(o.px);
  if (it == s.end())
    return;
  it->second.size -= o.size;
  it->second.disp -= o.disp;
  it->second.count -= 1;
  if (it->second.count == 0)
    s.erase(it);
}

void CoinbaseExchangeSync::push_mbo(uint8_t action, const Order &o, bool recovery, uint64_t recv_ns,
                                    uint64_t send_ns)
{
  bfile::l3_mbo_v2_t r{};
  r.typ = en::l3::MBO_V2;
  r.venue = cfg_.spec.venue;
  r.transactTime = event_tx_;
  r.sendingTime = send_ns ? send_ns : event_tx_;
  r.handlerendtim = recv_ns;
  r.orderUpdateAction = action;
  r.securityID = cfg_.spec.sec_id;
  r.orderID = o.oid;
  r.priority = prio_.count(o.uuid) ? prio_[o.uuid] : 0;
  r.pxd = double(o.px);
  r.displayQty = o.disp;
  r.side = o.bid ? '0' : '1';
  r.recovery = recovery;
  out_.emplace_back(r);
}

void CoinbaseExchangeSync::add(const Event &ev, bool recovery, uint64_t recv_ns, uint64_t send_ns)
{
  if (orders_.count(ev.order))
  {
    ++st_.dup_open;
    return;
  }
  if (!ev.has_px || !ev.has_size || (ev.side != 'b' && ev.side != 's'))
  {
    ++st_.bad_message;
    return;
  }
  Order o;
  o.uuid = ev.order;
  o.bid = ev.side == 'b';
  o.px = ev.px;
  o.size = ev.size;
  bool sat = false;
  o.disp = cfg_.spec.display(o.size, &sat);
  if (sat)
    ++st_.saturated;
  if (o.disp == 0)
  {
    // an open with no size: nothing rests
    ++st_.bad_message;
    return;
  }
  o.oid = make_oid(o.uuid);
  prio_[o.uuid] = next_prio_++;
  auto &slot = orders_[o.uuid];
  slot = o;
  level_add(slot);
  push_mbo(0, slot, recovery, recv_ns, send_ns);
}

void CoinbaseExchangeSync::remove(Order &o, uint64_t recv_ns, uint64_t send_ns)
{
  level_sub(o);
  push_mbo(2, o, false, recv_ns, send_ns);
  free_oid(o);
  prio_.erase(o.uuid);
  Uuid u = o.uuid;
  orders_.erase(u);
}

void CoinbaseExchangeSync::set_size(Order &o, int64_t new_size, uint64_t recv_ns, uint64_t send_ns)
{
  if (new_size <= 0)
    return remove(o, recv_ns, send_ns);
  level_sub(o);
  if (new_size > o.size)
    prio_[o.uuid] = next_prio_++; // a larger size goes to the back (Coinbase and OB agree)
  o.size = new_size;
  bool sat = false;
  o.disp = cfg_.spec.display(o.size, &sat);
  if (sat)
    ++st_.saturated;
  level_add(o);
  push_mbo(1, o, false, recv_ns, send_ns);
}

void CoinbaseExchangeSync::move(Order &o, int64_t new_px, int64_t new_size, uint64_t recv_ns, uint64_t send_ns)
{
  if (new_size <= 0)
    return remove(o, recv_ns, send_ns);
  level_sub(o);
  prio_[o.uuid] = next_prio_++; // a price change goes to the back
  o.px = new_px;
  o.size = new_size;
  bool sat = false;
  o.disp = cfg_.spec.display(o.size, &sat);
  if (sat)
    ++st_.saturated;
  level_add(o);
  push_mbo(1, o, false, recv_ns, send_ns);
}

void CoinbaseExchangeSync::clear_book()
{
  orders_.clear();
  live_ids_.clear();
  prio_.clear();
  bids_.clear();
  asks_.clear();
}

void CoinbaseExchangeSync::check_cross()
{
  if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first && status_ == 17)
    ++st_.crossed_events;
}

void CoinbaseExchangeSync::apply(const Event &ev)
{
  ++st_.applied;
  begin(ev);
  const uint64_t send = ev.time_ns;
  switch (ev.type)
  {
  case Event::Received:
    ++st_.received;
    break;
  case Event::Open:
    ++st_.opens;
    add(ev, false, ev.recv_ns, send);
    break;
  case Event::Done:
  {
    ++st_.dones;
    auto it = orders_.find(ev.order);
    if (it == orders_.end())
      ++st_.ignored_done; // never rested, or already removed by its last match
    else
      remove(it->second, ev.recv_ns, send);
    break;
  }
  case Event::Change:
  {
    ++st_.changes;
    auto it = orders_.find(ev.order);
    if (it == orders_.end())
    {
      ++st_.ignored_change; // received but not open: not on the book
      break;
    }
    auto &o = it->second;
    int64_t px = ev.has_px ? ev.px : o.px;
    int64_t sz = ev.has_size ? ev.size : o.size;
    if (px != o.px)
      move(o, px, sz, ev.recv_ns, send);
    else if (sz != o.size)
      set_size(o, sz, ev.recv_ns, send);
    break;
  }
  case Event::Match:
  {
    ++st_.matches;
    auto it = orders_.find(ev.maker);
    // the print, whether or not the maker is known
    if (ev.has_px && ev.has_size)
    {
      bfile::l3_mbp_trd_t p{};
      p.typ = en::l3::MBPT;
      p.venue = cfg_.spec.venue;
      p.transactTime = event_tx_;
      p.sendingTime = send ? send : event_tx_;
      p.handlerendtim = ev.recv_ns;
      p.securityID = cfg_.spec.sec_id;
      p.pxd = double(ev.px);
      // `side` is the maker's: a sell maker means a buy aggressor (CME
      // AggressorSide 1 buy, 2 sell)
      p.aggresorSide = ev.side == 's' ? 1 : ev.side == 'b' ? 2 : 0;
      p.sz = int32_t(std::min<int64_t>(cfg_.spec.display(ev.size), std::numeric_limits<int32_t>::max()));
      p.numorders = 1;
      p.lastTrade = true;
      out_.emplace_back(p);
    }
    if (it == orders_.end())
    {
      ++st_.unknown_maker;
      if (cfg_.check_gaps)
      {
        out_.clear();
        gap("match on an order not in the book", ev.recv_ns);
        return;
      }
      break;
    }
    auto &o = it->second;
    if (ev.has_px && ev.px != o.px)
      ++st_.match_px_mismatch;
    int64_t left = o.size - ev.size;
    if (left < 0)
    {
      ++st_.overfill;
      left = 0;
      if (cfg_.check_gaps)
      {
        out_.clear();
        gap("match larger than the maker's size", ev.recv_ns);
        return;
      }
    }
    uint32_t after = cfg_.spec.display(left);
    int64_t last_qty = int64_t(o.disp) - int64_t(after);
    if (last_qty > 0)
    {
      bfile::l3_mbo_trd_v2_t x{};
      x.typ = en::l3::MBOT_V2;
      x.venue = cfg_.spec.venue;
      x.recv_time = ev.recv_ns;
      x.transactTime = event_tx_;
      x.sendingTime = send ? send : event_tx_;
      x.handlerendtim = ev.recv_ns;
      x.lastQty = int32_t(std::min<int64_t>(last_qty, std::numeric_limits<int32_t>::max()));
      x.orderID = o.oid;
      x.lastTrade = true;
      out_.emplace_back(x);
    }
    else
      ++st_.zero_execs; // the fill is smaller than one display unit
    if (left == 0)
      remove(o, ev.recv_ns, send);
    else
    {
      // a match never changes priority: size goes down in place
      level_sub(o);
      o.size = left;
      o.disp = after;
      level_add(o);
      push_mbo(1, o, false, ev.recv_ns, send);
    }
    break;
  }
  default:
    ++st_.other;
    break;
  }
  end_event(ev.seq, send);
  check_cross();
}

}
