/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "actors/act/Manager.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/FillSub.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/GetPNL.hpp"
#include "frame/som/msg/PNL.hpp"
#include "frame/som/msg/SOMAction.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/mda/msg/Data.hpp"
#include "enum/e_names.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/som/act/SOM.hpp"
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include "frame/som/msg/Start.hpp"
#include "frame/som/msg/Stop.hpp"
#include "chutil/Table.hpp"
#include <sstream>
#include <boost/algorithm/string/predicate.hpp>

// Position persistence messages
#include "frame/som/msg/GetPosition.hpp"
#include "frame/som/msg/PositionResponse.hpp"
#include "frame/som/msg/UpdatePosition.hpp"
#include "frame/som/msg/ResetPositions.hpp"

using namespace std;
using namespace frame;
using namespace frame::som;

#define DISABLE_STASHING

act::SOM::SOM(
    en::x venue,
    actor_ptr _db,
    const boost::property_tree::ptree &_pt,
    const vector<vector<actor_ptr>> &_order_books,
    bool _sim_mode,
    bool spin,
    bool _reset_positions,
    actor_ptr _fill_subscriber)
    : Actor(),
      pt(_pt),
      curr_id(venue * 1000000),
      order_books(_order_books),
      sim_mode(_sim_mode),
      stopped(false), stashing(true),
      reset_positions(_reset_positions),
      db(_db)
{
  (void)spin;

  snprintf(name, sizeof(name), "SOM_%s", en::to_string(venue));

  // CRITICAL: Position reset will be handled in start_handler
  if (reset_positions && db)
  {
    log_inf("Position reset requested - will clear database on startup");
  }

  if (sim_mode)
  {
    // In sim mode, verify we have at least one order book for this venue
    const auto &books_for_x = order_books[venue];
    bool has_ob = false;
    for (size_t i = 0; i < books_for_x.size(); i++)
    {
      if (books_for_x[i] != nullptr)
      {
        has_ob = true;
        break;
      }
    }
    ASSERTF(has_ob, boost::format("no order books set for SOM: %1%") % name);
  }

  // ack delay is not being used
  // ack_delay = pt.get<double>("ack_delay", 2000); // for canc acks
  // ack_delay *= 1000;

  const std::size_t n = ref::RefData::inst().num_assets();
  pos.resize(en::trader_num_syms());

  for (std::size_t k = 0; k < en::trader_num_syms(); k++)
  {
    pos[k].resize(n);
  }

  read_limits();

  best_ask.resize(n);
  best_bid.resize(n);

  fill_volume.resize(n, 0);
  num_ords.resize(n, 0);
  num_cr.resize(n, 0);
  order_volume.resize(n, 0);
  num_canc.resize(n, 0);
  num_fills.resize(n, 0);
  num_xcnrej = 0;
  num_icnrej.resize(n, 0);
  num_xorej.resize(n, 0);
  num_ican = 0;

  MESSAGE_HANDLER(msg::Order, order_handler);
  MESSAGE_HANDLER(msg::Cancel, canc_handler);
  MESSAGE_HANDLER(msg::CancAck, canc_ack_handler);
  MESSAGE_HANDLER(ob::msg::BBBOChg, bbbochg_handler);

  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(msg::CancReject, canc_rej_handler);
  MESSAGE_HANDLER(msg::Fill, fill_handler);

  MESSAGE_HANDLER(msg::FillSub, fill_sub_handler);
  MESSAGE_HANDLER(som::msg::Stop, stop_trading_handler);
  MESSAGE_HANDLER(som::msg::Start, start_trading_handler);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);

  MESSAGE_HANDLER(msg::Ack, ack_handler);
  MESSAGE_HANDLER(msg::Reject, rej_handler);
  MESSAGE_HANDLER(msg::GetPNL, get_pnl_handler);

  MESSAGE_HANDLER(msg::UnStash, unstash_handler);
  MESSAGE_HANDLER(msg::ExitPos, exit_handler);
  MESSAGE_HANDLER(msg::RiskInfo, riskinfo_handler);

  MESSAGE_HANDLER(cons::msg::Get, get_handler);

  MESSAGE_HANDLER(msg::SetExchManager, set_exch_manager_handler);

  // Position persistence message handler
  MESSAGE_HANDLER(frame::som::msg::PositionResponse, position_response_handler);

  order_managers.resize(en::x_num_syms(), 0);

  // If a local fill subscriber was provided, add it directly
  if (_fill_subscriber)
  {
    subs.push_back(_fill_subscriber);
    log_inf("Added local fill subscriber: %s", _fill_subscriber->get_name());
  }
}

void act::SOM::add_fill_subscriber(actor_ptr subscriber)
{
  if (subscriber)
  {
    subs.push_back(subscriber);
    log_inf("Added fill subscriber: %s", subscriber->get_name());
  }
}

void act::SOM::read_limits()
{
  const std::size_t n = ref::RefData::inst().num_assets();
  // get position limits from config
  pos_limit.resize(n, 0);
  auto pt_lim = pt.get_child("som.pos_limit");
  for (auto &p : pt_lim)
  {
    for (std::size_t i = 0; i < n; i++)
    {
      auto a = ref::RefData::inst().asset(i);
      if (boost::algorithm::starts_with(a->name, p.first))
      {
        pos_limit[i] = p.second.get_value<int>();
        std::cout << "SOM pos_limit[" << a->name << "] = " << pos_limit[i] << std::endl;
        //log_inf("SOM pos_limit[%s] = %d", a->name.c_str(), pos_limit[i]);
      }
    }
  }
  // get size limits from config
  size_limit.resize(n, 0);
  auto pt_szlim = pt.get_child("som.size_limit");
  for (auto &p : pt_szlim)
  {
    for (std::size_t i = 0; i < n; i++)
    {
      auto a = ref::RefData::inst().asset(i);
      if (boost::algorithm::starts_with(a->name, p.first))
      {
        size_limit[i] = p.second.get_value<int>();
        std::cout << "SOM size_limit[" << a->name << "] = " << size_limit[i] << std::endl;
        //log_inf("SOM size_limit[%s] = %d", a->name.c_str(), size_limit[i]);
      }
    }
  }
}

// this function is a bottleneck
uint32_t act::SOM::get_working_orders_sz(int sym, en::bs side, en::trader trader) const noexcept
{
  uint32_t ret = 0;

  for (const auto &[k, order] : orders)
  {
    if (order.sym != sym || order.side != side || order.owner != trader)
      continue;
    auto filled = order.filled;
    auto canced = order.canced;
    auto rejed = order.rejed;
    if (filled || canced || rejed)
      continue;
    ret += order.sz;
  }

  return ret;
}

int act::SOM::get_pos(int sym, en::trader trader) const noexcept
{
  return pos[trader][sym].totalSize();
}

/**
 * @brief this function makes no sense should not be used
 *
 * @param owner
 * @return int
 */
int act::SOM::tot_pos(en::trader owner) const noexcept
{
  auto pos_for_owner = [this](auto owner)
  {
    int ret = 0;
    for (uint i = 0; i < pos[owner].size(); ++i)
    {
      ret += pos[owner][i].totalSize();
    }
    return ret;
  };
  if (owner == en::trader::ALL)
  {
    int sum = 0;
    for (std::size_t i = 0; i < en::trader_num_syms(); i++)
    {
      sum += pos_for_owner(i);
    }
    return sum;
  }
  else
  {
    return pos_for_owner(owner);
  }
}

string
act::SOM::get_pos_string(en::trader owner) const noexcept
{
  SNGH;
  vector<string> cols = {"sym", "pos"};
  chutil::Table t("Position", cols);
  for (uint i = 0; i < pos[owner].size(); ++i)
  {
    t.add_row();
    t.set_value(i);
    t.set_value(pos[owner][i].totalSize());
  }
  return t.to_string();
}

string
act::SOM::get_status([[maybe_unused]] en::trader owner) const noexcept
{

  // auto pos = tot_pos(owner);
  // auto pnl = tot_pnl(owner);

  uint32_t
      tot_fill_vol = 0,
      tot_num_canc = 0,
      tot_num_ords = 0,
      tot_num_cr = 0,
      //tot_num_fill = 0,
      tot_num_icnrej = 0,
      tot_num_xord_rej = 0;
  for (std::size_t i = 0; i < fill_volume.size(); i++)
  {
    tot_fill_vol += fill_volume[i];
    tot_num_ords += num_ords[i];
    tot_num_cr += num_cr[i];
    //tot_num_fill += num_fills[i];
    tot_num_canc += num_canc[i] - num_cr[i];
    tot_num_icnrej += num_icnrej[i];
    tot_num_xord_rej += num_xorej[i];
  }

  vector<string> cols = {"k", "v"};
  chutil::Table t("Status", cols);

  // t.add_row();
  // t.set_value("P/L");
  // t.set_value(pnl);

  // t.add_row();
  // t.set_value("P/C");
  // t.set_value(tot_fill_vol > 0 ? pnl / tot_fill_vol : 0);

  // t.add_row();
  // t.set_value("POS");
  // t.set_value(pos);

  t.add_row();
  t.set_value("VOL");
  t.set_value(tot_fill_vol);

  t.add_row();
  t.set_value("ORD");
  t.set_value(tot_num_ords);

  t.add_row();
  t.set_value("CAN");
  t.set_value(tot_num_canc);

  t.add_row();
  t.set_value("CR");
  t.set_value(tot_num_cr);

  t.add_row();
  t.set_value("ICNR");
  t.set_value(tot_num_icnrej);

  t.add_row();
  t.set_value("XRJ");
  t.set_value(tot_num_xord_rej);

  t.add_row();
  t.set_value("XCR");
  t.set_value(num_xcnrej);

  return t.to_string();
}

void act::SOM::set_exch_manager_handler(const msg::SetExchManager *m) noexcept
{
  if (m->manager)
  {
    log_inf("set_exch_manager_handler venue: %s, handler: %s",
            en::to_string(m->venue),
            m->manager->get_name());
  }
  else
  {
    log_inf("set_exch_manager_handler venue: %s, handler: NULL",
            en::to_string(m->venue));
  }
  order_managers[m->venue] = m->manager;
}

void act::SOM::get_handler(const cons::msg::Get *m) noexcept
{

  if (m->what == "cancall")
  {
    cancel_all_orders();
    reply(new cons::msg::Page("OK"));
    return;
  }
  else if (m->what == "pnl")
  {
    auto owner_it = m->kv.find("owner");
    if (owner_it == m->kv.end())
    {
      reply(new cons::msg::Page("key owner not found"));
      return;
    }
    auto owner_s = owner_it->second;
    auto valid = en::trader_is_valid(owner_s.c_str());
    if (!valid)
    {
      reply(new cons::msg::Page("invalid owner"));
      return;
    }
    auto owner = en::trader_index_of(owner_s);
    auto __pnl = get<0>(get_pnl(owner));
    vector<string> cols = {"sym", "pos", "pnl"};
    chutil::Table t("PnL", cols);
    for (uint i = 0; i < __pnl.size(); ++i)
    {
      t.add_row();
      auto a = ref::RefData::inst().asset(i);
      ASSERT(a, "must be found");
      t.set_value(a->name);
      t.set_value(get_pos(i, owner));
      t.set_value(__pnl[i]);
    }
    reply(new cons::msg::Page(t.to_string()));
    return;
  }
  else if (m->what == "limits")
  {
    vector<string> cols = {"sym", "pos_limit", "size_limit"};
    chutil::Table t("Limits", cols);
    for (uint i = 0; i < pos_limit.size(); ++i)
    {
      t.add_row();
      auto a = ref::RefData::inst().asset(i);
      if (!a)
        continue;
      t.set_value(a->name);
      t.set_value(pos_limit[i]);
      t.set_value(size_limit[i]);
    }
    reply(new cons::msg::Page(t.to_string()));
    return;
  }
  else if (m->what == "pos")
  {
    // SNGH;
    // reply(new cons::msg::Page(get_pos_string()));
    // return;
  }
  else if (m->what == "status")
  {
    reply(new cons::msg::Page(get_status(en::trader::ALL)));
    return;
  }
  else if (m->what == "working")
  {
    vector<string> labels = {"ts", "id", "status", "venue", "sym", "px", "sz", "side", "stbf", "owner"};
    chutil::Table t("Orders", labels);

    for (const auto &[k, order] : orders)
    {
      t.add_row();

      auto filled = order.filled;
      auto canced = order.canced;
      auto rejed = order.rejed;

      if (canced)
        continue;

      auto tim = chutil::Time::from_epoch(order.ts);
      t.set_value(tim.to_string(3));

      t.set_value(mda::OrderID::id(order.oid));
      if (filled)
      {
        t.set_value("FILLED   ");
      }
      else if (canced)
      {
        t.set_value("CANCELLED");
      }
      else if (rejed)
      {
        t.set_value("REJECTED ");
      }
      else
        t.set_value("WORKING  ");
      t.set_value(en::to_string(order.venue));
      t.set_value(ref::RefData::inst().asset(order.sym)->name);
      t.set_value(order.px);
      t.set_value(order.sz);
      t.set_value(en::to_string(order.side));
      t.set_value(order.still_to_be_filled);
      t.set_value(en::to_string(order.owner));
    }

    reply(new cons::msg::Page(t.to_string()));
  }
  else if (m->what == "orders")
  {
    vector<string> labels = {"ts", "id", "status", "venue", "sym", "px", "sz", "side", "stbf", "owner"};
    chutil::Table t("Orders", labels);

    for (const auto &[k, order] : orders)
    {
      t.add_row();

      auto filled = order.filled;
      auto canced = order.canced;
      auto rejed = order.rejed;

      t.set_value(chutil::Time::from_epoch(order.ts).to_string(3));
      t.set_value(mda::OrderID::id(order.oid));
      if (filled)
        t.set_value("FILLED   ");
      else if (canced)
        t.set_value("CANCELLED");
      else if (rejed)
        t.set_value("REJECTED ");
      else
        t.set_value("WORKING  ");
      t.set_value(en::to_string(order.venue));
      t.set_value(ref::RefData::inst().asset(order.sym)->name);
      t.set_value(order.px);
      t.set_value(order.sz);
      t.set_value(en::to_string(order.side));
      t.set_value(order.still_to_be_filled);
      t.set_value(en::to_string(order.owner));
    }


    reply(new cons::msg::Page(t.to_string()));
  }
  else if (m->what == "fills")
  {
    //
    // crashing here
    //
    // vector<string> labels = {"ts", "id", "venue", "sym", "px", "sz", "side", "stbf", "owner"};
    // chutil::Table t("Fills", labels);
    // CA &keys = orders.all_keys();
    // for (auto k : keys)
    // {
    //   const msg::Order *order=0;
    //   auto found = orders.get(k, order);
    //   ASSERT(found, "must be found");

    //   t.add_row();

    //   auto filled = filled_orders.get(k);

    //   if (!filled)
    //     continue;

    //   auto tim = chutil::Time::from_epoch(order->ts);
    //   t.set_value(tim.to_string(3));
    //   t.set_value(mda::OrderID::id(order->oid));
    //   t.set_value("FILLED");
    //   t.set_value(en::to_string(order->venue));
    //   t.set_value(ref::RefData::inst().asset(order->sym)->name);
    //   t.set_value(order->px);
    //   t.set_value(order->sz);
    //   t.set_value(en::to_string(order->side));
    //   t.set_value(order->still_to_be_filled);
    //   t.set_value(en::to_string(order->owner));
    // }
    // reply(new cons::msg::Page(t.to_string()));
  }
  else if (m->what == "readlimits")
  {
    read_limits();
    reply(new cons::msg::Page("read limits"));
  }
}

// not used
void act::SOM::riskinfo_handler(const msg::RiskInfo *) noexcept
{
  log_err("riskinfo_handler not implemented");
  // if (m->pnl < -maxloss)
  // {
  //   log_err("PNL below threshold pnl: %f", m->pnl);
  //   log_opr("PNL below threshold pnl: %f", m->pnl);
  //   send(new msg::ExitPos(m->venu));
  // }
}

void act::SOM::exit_handler(const msg::ExitPos *m) noexcept
{
  log_opr("exit_handler svenue: %s", en::to_string(m->venu));
  ASSERT(!sim_mode, "not available in sim");
  cancel_all_orders();
  auto itVenue = order_managers[m->venu];
  auto fwd_msg = new msg::ExitPos(*m);
  itVenue->send(fwd_msg);
  log_opr("trading is off");
  stopped = true;
}

void act::SOM::unstash_handler(
#ifdef DISABLE_STASHING
    const msg::UnStash *) noexcept
#else
    const msg::UnStash *m) noexcept
#endif
{
#ifdef DISABLE_STASHING
#else
  currts = m->ts;
  if (m->ts != 0)
  {
    log_tim("TIMESTAMP UNSTASH: t_handler=%lu, t1=%lu",
            m->ts,
            chutil::Time::epoch());
  }
  unstash(buy_orders_q, buy_cancel_q);
  unstash(sel_orders_q, sel_cancel_q);
#endif
}

void act::SOM::stash(const msg::Cancel *m) noexcept
{

  const msg::Order *order=0;
  bool found = false;

  auto ptr_ = orders.find(m->id);
  if (ptr_ != orders.end())
  {
    order = &ptr_->second;
    found = true;
  }

  if (!found)
  {
    log_err("in stash got cancel or an order that was not found");
    not_found_cancel_q[m->id] = *m;
  }
  else
  {
    if (order->side == en::bs::BUY)
    {
      buy_cancel_q[m->id] = *m;
    }
    else
    {
      sel_cancel_q[m->id] = *m;
    }
  }
}

void act::SOM::stash(const msg::Order *m) noexcept
{
  if (m->side == en::bs::BUY)
    buy_orders_q[m->oid] = *m;
  else
    sel_orders_q[m->oid] = *m;
}

void act::SOM::unstash(
    std::map<int, frame::som::msg::Order> &orders_q,
    std::map<int, frame::som::msg::Cancel> &cancels_q) noexcept
{

  stashing = false;

  auto p1 = orders_q.begin();
  auto p2 = cancels_q.begin();

#ifdef CREATECANCREPL
  while (p1 != orders_q.end() && p2 != cancels_q.end())
  {
    if (p1->second.oid == (int)p2->second.id)
      break;
    auto *ord = &p1->second;
    ord->oid_to_cancel = p2->second.id;
    log_dbg("creating cancrepl orderid: %d, cancid: %d", ord->oid, ord->oid_to_cancel);
    order_handler(ord);
    ++p1;
    ++p2;
  }
#endif

  while (p1 != orders_q.end())
  {
    auto *ord = &p1->second;
    order_handler(ord);
    ++p1;
  }

  while (p2 != cancels_q.end())
  {
    auto *ord = &p2->second;
    canc_handler(ord);
    ++p2;
  }

  cancels_q.clear();
  orders_q.clear();

  auto p3 = not_found_cancel_q.begin();
  while (p3 != not_found_cancel_q.end())
  {
    auto *ord = &p3->second;
    canc_handler(ord);
    ++p3;
  }

  not_found_cancel_q.clear();

  stashing = true;
}

void act::SOM::start_handler(const actors::msg::Start *) noexcept
{
  // Position persistence: reset or restore positions
  if (db)
  {
    if (reset_positions)
    {
      // Send ResetPositions message to DB to clear all positions
      log_inf("Sending ResetPositions to DB");
      db->send(new frame::som::msg::ResetPositions(), this);
    }
    else
    {
      // Query DB for current positions for each (trader, sym) pair
      log_inf("Querying DB for persisted positions");
      const std::size_t n = ref::RefData::inst().num_assets();
      for (std::size_t trader_idx = 0; trader_idx < en::trader_num_syms(); trader_idx++)
      {
        for (std::size_t sym = 0; sym < n; sym++)
        {
          en::trader trader = static_cast<en::trader>(trader_idx);
          db->send(new frame::som::msg::GetPosition(sym, trader), this);
        }
      }
    }
  }

  if (!order_books.empty())
  {
    for (auto &b_ : order_books)
    {
      for (auto &b : b_)
      {
        if (b)
          b->send(new ob::msg::BBBOSub(), this);
      }
    }
  }
  if (sim_mode)
    log_opr("running in sim mode");
  else
    log_opr("running in live exec mode");
}

void act::SOM::start_trading_handler(const som::msg::Start *) noexcept
{
  log_opr("trading is on");
  stopped = false;
}

void act::SOM::stop_trading_handler(const som::msg::Stop *) noexcept
{
  log_opr("trading is off");
  stopped = true;
  cancel_all_orders();
}

void act::SOM::cancel_all_orders() noexcept
{
  log_opr("cancelling all orders");

  for (const auto &[k, order] : orders)
  {
    auto filled = order.filled;
    auto canced = order.canced;
    auto rejed = order.rejed;

    if (filled || canced || rejed)
      continue;

    log_opr("cancelling on stop order: %d", k);
    cancel_order(k, order);
  }

}

void act::SOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
{

  cout << get_long_pnl_string(en::trader::ALL) << endl;

  if (diff_cnt == 0)
    cout << "time stamps not collected\n";
  else
    cout << "TIME_STAMP cnt: " << diff_cnt << " ave: " << diff_sum / diff_cnt << " ns\n";

  cout.flush();
}

void act::SOM::canc_rej_handler(const msg::CancReject *m) noexcept
{

  log_opr("CancReject  id: %d, xordid: %lu", m->id, m->xordid);

  if (db)
  {
    auto data = new msg::SOMAction(
      en::som::CANREJ,
      m->id);
      db->send(data, this);
  }

  const msg::Order *order=0;
  bool found = false;
  auto ptr_ = orders.find(m->id);
  if (ptr_ != orders.end())
  {
    order = &ptr_->second;
    found = true;
  }

  if (!found)
  {
    log_err("got CancReject for id: %d, but it was not found", m->id);
    return;
  }
  log_inf("got CancReject from exchange id: %d", m->id);
  // forward it on
  auto client = order->sender;
  if (client == this)
  {
    log_err("this is an order SOM originated");
    SNGH; // ??
  }
  else
  {

#ifdef TRACEORDERS
    cout << ">>> XCREJ: " << m->id << endl;
#endif

    num_xcnrej++;


    if (order->filled)
    {
      log_opr("sending canc reject its filled already");
      client->send(new msg::CancReject(m->id, msg::CancReject::FILLEDALREADY), this);
      return;
    }
    if (order->canced)
    {
      log_opr("sending canc reject its cancelled already");
      client->send(new msg::CancReject(m->id, msg::CancReject::CANCELLEDALREADY), this);
      return;
    }
    if (order->rejed)
    {
      log_err("sending canc reject its for a rejected order already");
      client->send(new msg::CancReject(m->id, msg::CancReject::REJECTEDPREVIOUSLY), this);
      return;
    }

    client->send(new msg::CancReject(m->id, m->reason, m->ord_status), this);
    log_inf("sending canc reject unknown");
  }
}

// from exchange
void act::SOM::ack_handler(const som::msg::Ack *m) noexcept
{

  log_opr("got ack from exchage for id: %d, xordid: %lu", m->id, m->xordid);

  auto ptr_ = orders.find(m->id);
  ASSERT(ptr_ != orders.end(), "must be found");
  msg::Order *order = &ptr_->second;

  order->acked = true;

  if (db)
  {
    auto data = new msg::SOMAction(
      en::som::ACK,
      m->id);
    db->send(data, this);
  }

  if (order->unacked_slated_for_canc)
  {
    log_inf("got ack for order slated for cancel id: %d", m->id);
    if (order->canced)
    {
      log_inf("but have canc ack already");
    }
    else
    {

      ASSERT(uint(order->oid) == m->id, "must be same id");
      cancel_order(m->id, *order);
    }
  }
}

void act::SOM::rej_handler(const som::msg::Reject *m) noexcept
{

  if (db)
  {
    auto data = new msg::SOMAction(
      en::som::REJ,
      m->id);
    db->send(data, this);
  }

  msg::Order *ord=0;
  bool found = false;
  auto ptr_ = orders.find(m->id);
  if (ptr_ != orders.end())
  {
    ord = &ptr_->second;
    found = true;
  }

  if (!found)
  {
    log_err("got REJECT for id: %d, but it was not found", m->id);
    // ERR("unknown reject");
    return;
  }

  num_xorej[ord->sym]++;

#ifdef TRACEORDERS
  cout << ">>> XCREJ: " << ord->sym << endl;
#endif

  log_rej("Reject id: %d, venue: %s, px: %d, sz: %d, side: %s, stbf: %d",
          mda::OrderID::id(m->id),
          en::to_string(ord->venue),
          ord->px,
          ord->sz,
          en::to_string(ord->side),
          ord->still_to_be_filled);
  auto client = ord->sender;
  client->send(new msg::Reject(m->id, m->reason), this);
  ord->rejed = true;
  //rejected_orders.insert(m->id);
  orders.erase(m->id);
}

// from exchange
void act::SOM::fill_handler(const msg::Fill *m) noexcept
{

  ASSERT(m->tim > 0, "no tim");

  if (db)
  {
    auto data = new msg::SOMAction(
      en::som::FILL,
      m->id,
      m->sz);
    db->send(data, this);
  }

  fill_volume[m->sym] += m->sz;
  num_fills[m->sym]++;

  ASSERT(m->sz > 0, "invalid fill sz");
  msg::Order *ord = 0;
  bool found = false;
  auto ptr_ = orders.find(m->id);
  if (ptr_ != orders.end())
  {
    ord = &ptr_->second;
    found = true;
  }

  if (ord->canced)
  {
    log_err("got fill for a cancelled order");
    SNGH;
  }

  //auto found = orders.get(m->id, ord);
  auto a = ref::RefData::get_asset(m->sym);
  ASSERT(a, "no such asset");
  if (!found)
  {
    log_err("got fill but did not find the order: %d, sym: %s, px: %f, sz: %d, side: %s",
            m->id,
            a->name,
            m->px.to_double(),
            m->sz,
            en::to_string(m->side));
    return;
  }

  log_fil("Fill id: %d venue: %s, sym: %s, sym: %d, px: %f, px: %d, sz: %d, side: %s, stbf: %d, trader: %s",
          m->id,
          en::to_string(ord->venue),
          a->name,
          m->sym,
          m->px.to_double(),
          m->px.to_int(),
          m->sz,
          en::to_string(m->side),
          m->still_to_be_filled,
          en::to_string(m->owner));

  if (m->still_to_be_filled <= 0.0000001)
  {
    log_dbg("adding id: %d to filled orders", m->id);
    ord->filled = true;
  }
  else
  {
    log_dbg("not adding to filled orders");
  }
  // probably more information is passed here than needed
  // client should have sym and side info
  auto f = new msg::Fill(
      m->id,
      ord->venue,
      m->sym,
      m->px,
      m->side,
      m->sz,
      m->still_to_be_filled,
      m->owner,
      m->tim  
    );
  const_cast<msg::Order *>(ord)->still_to_be_filled = m->still_to_be_filled;
  ord->sender->send(f, this);
  if (m->still_to_be_filled < -0.0000001)
  {
    log_err("got fill but stb < 0, id: %d, stbf: %d",
            m->id, m->still_to_be_filled);
    // no need to erase them just keep it here ?
    //orders.erase(m->id);
  }
  //
  // its possible to listen in for fills if required
  //
  if (!subs.empty())
  {
    log_inf("Publishing Fill id=%d to %zu subscribers", m->id, subs.size());
  }
  for (auto s : subs)
  {
    auto f = new msg::Fill(
        m->id,
        ord->venue,
        m->sym,
        m->px,
        m->side,
        m->sz,
        m->still_to_be_filled,
        m->owner,
      m->tim  );
    s->send(f, this);
  }
  const auto &pos_ = pos[m->owner];
  if (pos_.size() > m->sym)
  {
    pos[m->owner][m->sym].addToPosition(pos::Position::PosEntry(m->side, m->px.to_int(), m->sz));

    // Position persistence: Send UpdatePosition to DB after fill
    if (db)
    {
      db->send(new frame::som::msg::UpdatePosition(m->sym, m->owner, m->side, m->sz), this);
      log_dbg("Sent UpdatePosition to DB: sym=%d trader=%d side=%s sz=%d",
              m->sym, static_cast<int>(m->owner), en::to_string(m->side), m->sz);
    }
  }

  if (sim_mode)
  {
    const auto &tpnl = get_pnl_string_assets(en::trader::OCCAMUST);
    const auto &totv = get_vol_string_assets();
    const auto &ords = get_ord_string_assets();
    log_inf("pnl: %s ** voL: %s ** ord: %s", tpnl, totv, ords);
  }
}

void act::SOM::fill_sub_handler(const msg::FillSub *m) noexcept
{
  // register as a fill subscriber
  log_inf("FillSub received from: %s", m->sender ? m->sender->get_name() : "NULL");
  subs.push_back(m->sender);
  log_inf("Total fill subscribers: %zu", subs.size());
}

void act::SOM::get_pnl_handler(const som::msg::GetPNL *m) noexcept
{
  const auto owner = m->owner;
  auto __pnl = get<0>(get_pnl(owner)); // 0 is tpnl index per asset --- why is the copy required?
  reply(new msg::PNL(pos[owner], __pnl));
}

void act::SOM::bbbochg_handler(const ob::msg::BBBOChg *m) noexcept
{
  best_bid[m->sym] = m->best_bid;
  best_ask[m->sym] = m->best_ask;
  currtim = m->tx_time;
}

void act::SOM::order_handler(const msg::Order *m) noexcept
{
  ASSERT(m->sender, "no sender");
  ASSERT(m->sz > 0, "order for 0 size");
  ASSERT(m->sz < CHOPIN_MAX_ORD_SZ, "bad size");

  //
  // check limits
  //
  auto a = ref::RefData::get_asset(m->sym);
  auto check_limit = !a->is_cusip_asset;
#ifdef DISABLE_STASHING
  if (check_limit)
#else
  if (stashing && check_limit)
#endif
  {

    // not using it for performance reasons, should double check if must use
#ifdef INCLUDE_WORKING_ORDERS_INLIMIT
    int working_sz = get_working_orders_sz(m->sym, m->side, m->owner);
#else
    int working_sz = 0;
#endif

    auto current_pos = get_pos(m->sym, m->owner);
    bool over_limit = false;

    if (m->side == en::bs::BUY)
    {
      if (current_pos + working_sz + m->sz > pos_limit[m->sym])
      {
        log_err("BUY POS WOULD GO OVER LIMIT current_pos: %d, working_sz: %d, sz: %d, pos_limit: %d",
                current_pos,
                working_sz,
                m->sz,
                pos_limit[m->sym]);
        over_limit = true;
      }
    }
    else
    {
      if (current_pos - working_sz - m->sz < -pos_limit[m->sym])
      {
        log_err("SELL POS WOULD GO OVER LIMIT current_pos: %d, working_sz: %d, sz: %d, pos_limit: %d",
                current_pos,
                working_sz,
                m->sz,
                pos_limit[m->sym]);
        over_limit = true;
      }
    }

    if (m->sz > size_limit[m->sym])
    {
      log_err("ORDER SIZE WOULD GO OVER LIMIT sz: %d, size_limit: %d",
              m->sz,
              size_limit[m->sym]);
      over_limit = true;
    }
    if (over_limit)
    {
      auto a = ref::RefData::inst().asset(m->sym);
      std::string sym = "unknown";
      if (a)
        sym = a->name;
      log_rej("POSITION or SIZE WOULD GO OVER LIMIT Reject sym: %s, working_sz: %d, current_pos: %d, pos_limit: %d, size_limit: %d, id: %d, venue: %s, px: %d, sz: %d, side: %s, stbf: %d",
              sym,
              working_sz,
              current_pos,
              pos_limit[m->sz],
              size_limit[m->sym],
              mda::OrderID::id(m->oid),
              en::to_string(m->venue),
              m->px,
              m->sz,
              en::to_string(m->side),
              m->still_to_be_filled);
      auto client = m->sender;
      auto f = new msg::Ack(curr_id);
      ASSERT(client, "cant reply");
      reply(f); // reply to fast send
      client->send(new msg::Reject(curr_id, msg::Reject::OVERLIMIT), this);
      // set rejected flag here?
      return;
    }
  }

  if (m->oid < 0) // order has no id yet
  {
    log_dbg("oid is negative, setting it to: %d", curr_id);
    const_cast<msg::Order *>(m)->oid = curr_id++;
    orders[m->oid]= *m;
  }

#ifdef DISABLE_STASHING

  auto f = new msg::Ack(m->oid);
  ASSERT(m->sender, "cant reply");
  reply(f); // reply to fast send

#else

  //
  // stashing is temporarily disabled in unstash message
  //
  if (stashing)
  {
    auto f = new msg::Ack(m->oid);
    ASSERT(m->sender, "cant reply");
    reply(f); // reply to fast send
    log_inf("new order stashed oid: %d", m->oid);
    stash(m);
    return;
  }

#endif

  //
  // for simulation only
  //
  add_ts[m->oid] = currts;

  if (m->ts != 0)
  {
    log_tim("TIMESTAMP SOMNEW: t_handler=%lu, t1=%lu",
            m->ts,
            chutil::Time::epoch());
  }

  if (db)
  {
    auto data = new msg::SOMAction(
      en::som::NEW,
      m->oid,
      m->sz,
      m->venue,
      m->sym,
      m->side,
      m->px,
      m->sender->get_name()
    );
    db->send(data, this);
  }

  log_opr("stashing off new order symid: %s, side: %s, px: %d",
          m->sym, en::to_string(m->side), m->px);

  if (m->oid_to_cancel >= 0)
  {
    log_inf("CancRepl id to canc: %d", m->oid_to_cancel);
    const msg::Order *ord=0;
    if (internal_reject(m->oid_to_cancel, ord))
    {
      // Canc Reject already sent
      // make this a regular order
      m->oid_to_cancel = -1;
      num_icnrej[m->sym]++;
#ifdef TRACEORDERS
      cout << ">>> ICREJ: " << m->oid_to_cancel << " " << m->oid << endl;
#endif
      num_ords[m->sym]++;
    }
    else
    {
      num_cr[m->sym]++;
#ifdef TRACEORDERS
      cout << ">>> CREP: " << m->oid_to_cancel << " " << m->oid << endl;
#endif
      if (sim_mode)
        cancel_order(m->oid_to_cancel, *ord);
    }
  }
  else
  {
#ifdef TRACEORDERS
    cout << ">>> NEWORD: " << m->oid << endl;
#endif
    num_ords[m->sym]++;
  }

  if (stopped)
  {
    // TODO: refactor: this code copied from below
    log_inf("trading is stopped");
    auto client = m->sender;
    auto rej_id = m->oid;
    client->send(new msg::Reject(rej_id, true), this);
    log_rej("SOM rejecting new order because trading is stopped");
    return;
  }
  if (sim_mode)
  {
    const auto &books_for_x = order_books[m->venue];
    auto book = books_for_x[m->sym];
    ASSERTF(book != 0, boost::format("book not set for sym: %d, venue: %s") % m->sym % en::to_string(m->venue));
    auto d = new mda::msg::Data();
    d->l3 = bfile::l3_sim_t();
    d->israw = false; // it actually defaults to false
    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(new mda::msg::data_pay_load());
    payload->mkt = en::x::SIM;
    payload->sym = m->sym;
    payload->px = ref::Price(m->px, m->sym);
    payload->sz = m->sz;
    payload->disp_sz = m->sz;
    payload->side = m->side;
    payload->order_ref = mda::OrderID::longid(en::x::SIM, m->oid);
    payload->action = en::mt::NONE;
    payload->ot = en::ot::LIMIT;
    payload->mev = en::md::ADD;
    payload->owner = m->owner;
    payload->ts0 = m->ts;
#ifdef TIMTRACE
    ASSERT(payload->ts0 > 0, "bad ts0");
#endif
    d->payload = payload;
    log_dbg("placed sim order id: %d on venue: %s, orderref: %lu", m->oid, en::to_string(m->venue), d->payload->order_ref);
    ASSERT(d->payload->is_valid(), "not valid");
    // auto ts = chutil::Time::get_ts().time_since_epoch().count();
    // auto diff = ts - m->ts;
    // diff_sum += diff;
    // diff_cnt++;
    book->send(d, this);
  }
  else
  {
    // this is a real order
    log_dbg("real order");
    auto om = order_managers[m->venue];
    if (!om)
    {
      log_err("no order manager for venue: %s", en::to_string(m->venue));
      return;
    }
    ASSERT(om != 0, "book not set");
    // forward this message on and set its id
    auto fwd_msg = new msg::Order(*m);
    //fwd_msg->destination = 0;
    fwd_msg->order_ref = mda::OrderID::longid(m->venue, m->oid);
    orders[m->oid]= *fwd_msg;
    // todo: the real order manager needs to check order to cancel in case of a canc repl ?
    om->send(fwd_msg, this);
    log_inf("placed order id: %d, on venue: %s, longid: %lu",
            m->oid, en::to_string(m->venue), fwd_msg->order_ref);
  }
}

// record internal rejects
//
// TODO: this function is likely slow
//
bool act::SOM::internal_reject(int id, const msg::Order *&ord) noexcept
{

  auto logrej = [this, id](uint64_t ts)
  {
    if (ts != 0)
    {
      log_tim("TIMESTAMP SOMIREJ: t_handler=%lu, t1=%lu",
              ts,
              chutil::Time::epoch());
    }

    if (db)
    {
      auto data = new msg::SOMAction(
        en::som::IREJ,
        id);
      db->send(data, this);
    }

  };

  msg::Order *order=0;
  bool found = false;
  auto ptr_ = orders.find(id);
  if (ptr_ != orders.end())
  {
    order = &ptr_->second;
    found = true;
  }

  //auto found = orders.get(id, order);
  if (!found)
  {
    // this can happen if order is rejected due to pos limit
    log_err("sending reject got cancel for id: %d, but order was not found", id);
    logrej(0);
    return true;
  }
  ord = order;

  // #define NOINTERALREJECT
#ifdef NOINTERALREJECT
  return false; // for testing only
#endif

  if (order->filled)
  {
    log_opr("sending reject its filled already id: %d", id);
    ord->sender->send(new msg::CancReject(id, msg::CancReject::FILLEDALREADY), this);
    logrej(order->ts);
    return true;
  }

  if (order->canced)
  {
    log_err("sending reject its cancelled already id: %d", id);
    ord->sender->send(new msg::CancReject(id, msg::CancReject::CANCELLEDALREADY), this);
    logrej(order->ts);
    return true;
  }

  if (order->rejed)
  {
    log_err("sending reject has been rejected already id: %d", id);
    ord->sender->send(new msg::CancReject(id, msg::CancReject::REJECTEDPREVIOUSLY), this);
    logrej(order->ts);
    return true;
  }

  //
  // reject canc for orders that have not been acked yet
  //
  // do this for sim only
  //
  // could be done for live however its probably better to let the exchange do the reject
  //

  // #define REJECTCANC_SIMMODE
  if (sim_mode)
  {
#ifdef REJECTCANC_SIMMODE
    // todo: this is not correct
    // peraps we the ob should do this
    auto p = add_ts.find(id);
    ASSERT(p != add_ts.end(), "dont have add ts for this id");
    auto diff = currts - p->second;
    if (diff < ack_delay)
    {
      log_inf("sending SIM canc reject as diff: %d, and ack_delay: %d, has not elapsed",
              diff, ack_delay);
      ord->sender->send(new msg::CancReject(id, msg::CancReject::NOTACKED), this);
      return true;
    }
#endif
  }
  else
  {
    // live trading

    if (!allow_unacked_cancels && !ord->acked)
    {
      log_inf("got canc for order id: %d, that has not been acked yet marking it for cancel", id);
      //unacked_slated_for_canc.insert(id);
      order->unacked_slated_for_canc = true;
      return false;
    }
  }

  return false;
}

void act::SOM::canc_handler(const msg::Cancel *m) noexcept
{
  // this message comes from client

#ifdef DISABLE_STASHING
#else
  //
  // stashing is temporarily disabled in unstash message
  //
  if (stashing)
  {
    stash(m); // fast_send will return immediately
    return;
  }
#endif

  // why?
  // ASSERT(!m->is_fast, "cannot use fast send for canc");

  if (m->sender)
    log_dbg("Cancel id: %d, sender: %s", m->id, m->sender->get_name());

  const msg::Order *ord=0;
  if (internal_reject(m->id, ord))
  {
    log_inf("got internal reject for id: %d", m->id);
    return;
  }

  cancel_order(m->id, *ord);
}

void act::SOM::canc_ack_handler(const msg::CancAck *m) noexcept
{
  //
  // this is a canc ack from an exchange, find original
  //

  if (db)
  {
    auto data = new msg::SOMAction(
      en::som::CANACK,
      m->id);
    db->send(data, this);
  }

  log_opr("CancAck id: %d", m->id);

  msg::Order *order=0;
  bool found = false;
  auto ptr_ = orders.find(m->id);
  if (ptr_ != orders.end())
  {
    order = &ptr_->second;
    found = true;
  }

  //auto found = orders.get(m->id, order);
  if (!found)
  {
    log_err("got CANCACK for id: %d, but it was not found", m->id);
    return;
  }

  if (order->filled)
  {
    log_err("canc ack for a filled order");
  }

  log_dbg("CancAck venue: %s, id: %d, fillsz: %d, sz: %d",
          en::to_string(order->venue),
          m->id,
          m->fillsz,
          m->sz);
  if (order->still_to_be_filled <= 0)
  {
    log_err("got a canc ack for ecn: %s, ordid: %d, but it stbf: %d",
            en::to_string(mda::OrderID::exchid(m->id)),
            mda::OrderID::id(m->id),
            order->still_to_be_filled);
  }
  // forward it on
  auto client = order->sender;
  if (client == this)
  {
    log_inf("this is an order SOM originated");
  }
  else
  {
    client->send(new msg::CancAck(m->id, m->fillsz, m->sz), this);
  }
  if (m->sz <= 0)
  {
    log_inf("adding id: %d to cancelled orders", m->id);
    order->canced = true;
    //cancelled_orders.insert(m->id);
    orders.erase(m->id);
  }
  else
  {
    log_inf("not adding to cancelled orders");
  }
}

std::tuple<int, int, int>
act::SOM::get_pnl_all(int sym) const noexcept
{
  int tot_unrel = 0;
  int tot_rpnl = 0;
  int tot_totpnl = 0;
  auto a = ref::RefData::get_asset(sym);
  ASSERT(a, "no asset");
  auto cs = a->contract_size;
  for (std::size_t owner = 0; owner < en::trader_num_syms(); owner++)
  {
    if (pos[owner].size() < uint(sym))
      continue;
    tot_unrel += round(pos[owner][sym].unrealPnl(
        ref::Price(best_bid[sym], sym).to_int(),
        ref::Price(best_ask[sym], sym).to_int()));
    tot_rpnl += round(pos[owner][sym].pnl());
  }
  tot_totpnl = tot_unrel + tot_rpnl;
  tot_unrel *= cs;
  tot_rpnl *= cs;
  tot_totpnl *= cs;
  return make_tuple(tot_unrel, tot_rpnl, tot_totpnl);
}

std::tuple<int, int, int>
act::SOM::get_pnl_all_all() const noexcept
{
  std::tuple<int, int, int> ret = {0, 0, 0};
  const std::size_t n = ref::RefData::inst().num_assets();
  for (std::size_t i = 0; i < n; i++)
  {
    const auto &pnl = get_pnl_all(i);
    get<0>(ret) += get<0>(pnl);
    get<1>(ret) += get<1>(pnl);
    get<2>(ret) += get<2>(pnl);
  }
  return ret;
}

tuple<
    vector<int>,
    vector<int>,
    vector<int>>
act::SOM::get_pnl(int owner) const noexcept
{
  const std::size_t n = ref::RefData::inst().num_assets();
  vector<int> tpnl;
  vector<int> upnl;
  vector<int> rpnl;

  if ((best_bid.size() == 0) || (best_ask.size() == 0))
  {
    tpnl.resize(n, 0);
    upnl.resize(n, 0);
    rpnl.resize(n, 0);
  }
  else
  {
    for (uint i = 0; i < pos[owner].size(); ++i)
    {
      auto a = ref::RefData::get_asset(i);
      ASSERT(a, "no asset");
      auto cs = a->contract_size;
      upnl.push_back((int)pos[owner][i].unrealPnl(
                         ref::Price(best_bid[i], i).to_int(),
                         ref::Price(best_ask[i], i).to_int()) *
                     cs);
      rpnl.push_back((int)pos[owner][i].pnl() * cs);
      tpnl.push_back(rpnl[i] + upnl[i]);
    }
  }
  ASSERT(tpnl.size() == n, "n");
  ASSERT(upnl.size() == n, "n");
  ASSERT(rpnl.size() == n, "n");
  return make_tuple(tpnl, upnl, rpnl);
}

string
act::SOM::get_pnl_string_assets(en::trader owner) const noexcept
{
  auto allpnl = get_pnl(owner);
  auto tpnl = get<0>(allpnl);
  ostringstream pnl_stream;
  for (std::size_t i = 0; i < tpnl.size(); ++i)
  {
    auto a = ref::RefData::inst().asset(i);
    ASSERT(a, "no asset");
    pnl_stream << a->name << ": " << tpnl[i] << "; ";
  }
  return pnl_stream.str();
}

string
act::SOM::get_ord_string_assets() const noexcept
{
  ostringstream ord_stream;
  for (std::size_t i = 0; i < num_ords.size(); ++i)
  {
    auto a = ref::RefData::inst().asset(i);
    ASSERT(a, "no asset");
    ord_stream << a->name << ": " << num_ords[i] << "; ";
  }
  return ord_stream.str();
}

string
act::SOM::get_vol_string_assets() const noexcept
{
  ostringstream vol_stream;
  for (std::size_t i = 0; i < fill_volume.size(); ++i)
  {
    auto a = ref::RefData::inst().asset(i);
    ASSERT(a, "no asset");
    vol_stream << a->name << ": " << fill_volume[i] << "; ";
  }
  return vol_stream.str();
}

string
act::SOM::get_pnl_string(en::trader owner) const noexcept
{
  string tpnl_s = "";
  double sum = 0;
  if (owner == en::trader::ALL)
  {
    const auto &pnl = get_pnl_all_all();
    sum = get<2>(pnl);
  }
  else
  {
    const auto &ret = get_pnl(owner);
    const auto &tpnl = get<0>(ret);
    for (std::size_t i = 0; i < tpnl.size(); i++)
    {
      sum += tpnl[i];
    }
  }
  tpnl_s.append(" TOT PNL: ").append(boost::lexical_cast<string>(sum)).append("; ");
  return tpnl_s;
}

double
act::SOM::tot_pnl(en::trader owner) const noexcept
{
  if (owner == en::trader::ALL)
  {
    const auto &pnl = get_pnl_all_all();
    return get<2>(pnl);
  }
  else
  {
    const auto &ret = get_pnl(owner);
    const auto &tpnl = get<0>(ret);
    double sum = 0;
    for (std::size_t i = 0; i < tpnl.size(); i++)
    {
      sum += tpnl[i];
    }
    return sum;
  }
}

string
act::SOM::get_long_pnl_string(en::trader owner) const noexcept
{

  ostringstream ostr;

  const auto tpnl_s = get_pnl_string(owner);
  ostr << tpnl_s << " ";

  uint32_t
      tot_fill_vol = 0,
      tot_num_canc = 0,
      tot_num_ords = 0,
      tot_num_cr = 0,
      tot_num_fill = 0,
      //tot_num_icnrej = 0,
      tot_num_xord_rej = 0;
  for (std::size_t i = 0; i < fill_volume.size(); i++)
  {

    tot_fill_vol += fill_volume[i];
    tot_num_ords += num_ords[i];
    tot_num_cr += num_cr[i];
    tot_num_fill += num_fills[i];
    tot_num_canc += num_canc[i] - num_cr[i];
    //tot_num_icnrej += num_icnrej[i];
    tot_num_xord_rej += num_xorej[i];
  }

  ostr
      << " VOL: " << tot_fill_vol
      << "; FIL: " << tot_num_fill
      << "; CAN: " << tot_num_canc
      << "; ORD: " << tot_num_ords
      << "; CNR: " << tot_num_cr
      << "; XCR: " << num_xcnrej
      << "; REJ: " << tot_num_xord_rej
      << "; ICN: " << num_ican;

  if (tot_fill_vol > 0)

    ostr
        << "; VR: " << (tot_num_canc * 3 + tot_num_cr) / float(tot_fill_vol)
        << ";";

  else

    ostr
        << "; VR: no fills ;";

  ostr
      << std::endl;

  return ostr.str();
}

void frame::som::act::SOM::cancel_order(uint id, const msg::Order &ord) noexcept
{

  //
  // should not be necessary but it happened once
  // todo: code from internal_reject should be moved here
  //
  if (ord.filled)
  {
    log_opr("sending reject its filled already id: %d", id);
    ord.sender->send(new msg::CancReject(id, msg::CancReject::FILLEDALREADY), this);
    return;
  }
  if (ord.canced)
  {
    log_err("sending reject its cancelled already id: %d", id);
    ord.sender->send(new msg::CancReject(id, msg::CancReject::CANCELLEDALREADY), this);
    return;
  }
  if (ord.rejed)
  {
    log_err("sending reject has been rejected already id: %d", id);
    ord.sender->send(new msg::CancReject(id, msg::CancReject::REJECTEDPREVIOUSLY), this);
    return;
  }

  log_inf("cancelling order id: %d", id);

  num_canc[ord.sym]++;
  if (sim_mode)
  {
    // translate into a mod message
    const msg::Order &o = ord;
    const auto &books_for_x = order_books[o.venue];
    auto book = books_for_x[o.sym];
    ASSERT(book != 0, "book not set");
    auto d = new mda::msg::Data();
    d->l3 = bfile::l3_sim_t();
    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(new mda::msg::data_pay_load);
    payload->mkt = en::x::SIM;
    payload->sym = o.sym;
    payload->px = ref::Price(o.px, o.sym);
    payload->sz = o.sz;
    payload->action = en::mt::CANCD;
    payload->side = o.side;
    payload->order_ref = mda::OrderID::longid(en::x::SIM, id);
    payload->mev = en::md::MOD;
    payload->disp_sz = 0;
    payload->ts0 = o.ts;
    ASSERT(payload->ts0 > 0, "bad ts0");
    d->israw = false; // it actually defaults to false
    d->payload = payload;
    log_dbg("placed sim order canc for id: %d, for venue: %s, orderref: %lu", id, en::to_string(o.venue), payload->order_ref);
    ASSERT(payload->is_valid(), "not valid");
    book->send(d, this);
  }
  else
  {
    // this is a real order canc
    // find the venue
    auto venue = ord.venue;
    auto itVenue = order_managers[venue];
    if (!itVenue)
    {
      log_err("no order manager for venue: %s", en::to_string(venue));
      return;
    }
    // forward this message on
    auto fwd_msg = new msg::Cancel(id);
    //fwd_msg->destination = 0;
    fwd_msg->order_ref = mda::OrderID::longid(venue, id);
    itVenue->send(fwd_msg, this);
    log_opr("placed CANCORD, id: %d, on venue: %s, longid: %lu",
            fwd_msg->id, en::to_string(venue), fwd_msg->order_ref);

    if (db)
    {
      auto data = new msg::SOMAction(
        en::som::CAN,
        id
      );
      db->send(data, this);
    }
  }
}

// =============================================================================
// Position Persistence Handlers
// =============================================================================

void act::SOM::position_response_handler(const frame::som::msg::PositionResponse *m) noexcept
{
  // Restore position from DB response
  int trader_idx = static_cast<int>(m->trader);
  int sym = m->sym;
  int restored_position = m->position;

  log_inf("PositionResponse: trader=%d sym=%d position=%d - restoring position",
          trader_idx, sym, restored_position);

  // Restore position by creating position entries
  // Note: This is a simplified restoration - we restore the net position but lose
  // detailed entry history (individual fills at different prices)
  if (restored_position != 0)
  {
    // Determine side and size from net position
    en::bs side = (restored_position > 0) ? en::bs::BUY : en::bs::SEL;
    int sz = abs(restored_position);

    // Use a synthetic price of 0 since we don't have actual fill prices
    // This will affect PnL calculation until new fills occur, but position
    // tracking will be correct
    pos[m->trader][sym].addToPosition(pos::Position::PosEntry(side, 0, sz));

    log_inf("Restored position: trader=%d sym=%d net_position=%d",
            trader_idx, sym, restored_position);
  }
}
