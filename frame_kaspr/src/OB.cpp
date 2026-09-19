
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <set>
#include <cstdlib>
#include <cstdio>
#include "chutil/FileSystem.hpp"
#include "chutil/ut.hpp"
#include "frame/ref/RefData.hpp"
#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/OrderQ.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include "frame/ob/msg/CancNotify.hpp"
#include "frame/ob/msg/Clear.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ob/msg/EndOfBurst2.hpp"
#include "frame/ob/msg/GapDected.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/som/msg/UnStash.hpp"
#include "frame/mda/OrderID.hpp"
#include "actors/msg/Start.hpp"
#include "frame/mtim//msg/Alarm.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ref/Price.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/ob/msg/CheckRes.hpp"
#include <random>
#include <limits>
#include "frame/ob/act/OB.hpp"
#include "chutil/hash_map.hpp"
#include <algorithm>
#include "frame/som/msg/Reject.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "oogsl/gvector.hpp"
#include "bfile/r_l3.hpp"

using namespace std;
using namespace frame;
using namespace frame::ob;

// NOXMKT is gone: the uncross it used to guard is now a RUNTIME decision on
// do_cross_check, at the four sites in add(). A commented-out #define meant the
// code could not be reached in any build anyone produced, which is why a
// separate -- and much worse -- implementation was nearly written instead.
//
// SIMULATION (do_cross_check true) does NOT uncross. A crossed book there means
// the reconstruction is wrong, so every fill after it is fiction and the
// slippage number the run exists to produce is worthless; the invariant in
// process_market_data aborts and the state is left intact to debug. That is how
// 20250210's stranded-order bug and 20250509's bad capture were both found.
//
// LIVE (do_cross_check false, kaspr.cpp) cannot abort -- that would leave real
// orders resting at CME with nothing managing them -- so it cancels the real
// contra orders the arriving order crossed through, and walks the inside past
// the levels it just emptied.
//
// Doing it HERE, in add(), is the whole point. The arriving order IS the
// aggressor, so the side needs no guessing, the crossed range is known
// (best_ask..px), the record continues to be processed normally, and the book
// is never left crossed for a later record to trip over. TachBook's
// UNCROSS_BOOK (frame/ob/act/TachBook.hpp:416) is the same design.

// #define DBG_MSG  // Disabled - enable only for local debugging to avoid production overhead

#define OBFILE cerr

act::OB::OB(
    actor_ptr binrec,
    bool do_cross_check,
    actors::Manager *_man,
    uint _sym,
    boost::property_tree::ptree &pt)
    : actors::Actor(),
      man(_man),
#ifdef DBG_MSG
      debug(true),
#else
      debug(false),
#endif
      print_stats(false),
      sym(_sym),
      ex_sym_id(0),
      do_cross_check(do_cross_check),
      binrec(binrec)
{
  // Set name FIRST, before MESSAGE_HANDLER which may use get_name()
  auto a = ref::RefData::inst().get_asset(sym);
  ASSERT(a, "no such asset");
  snprintf(name, sizeof(name), "OB_%s", a->name.c_str());
  std::cerr << "OB constructor set name=\"" << name << "\" for asset " << a->name << " (sym=" << sym << ")" << std::endl;

  if (debug)
  {
    obfile.open("ob.txt");
  }

  MESSAGE_HANDLER(frame::ob::msg::CheckBook, check_handler);
  MESSAGE_HANDLER(frame::ob::msg::CheckSim, check_sim_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Set, set_handler);
  MESSAGE_HANDLER(frame::mda::msg::Data, data_handler);
  MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);

  // Check if a->name contains ' '
  bool contains_space = a->name.find(' ') != std::string::npos;
  bool contains_p = a->name.find('P') != std::string::npos;
  bool contains_c = a->name.find('C') != std::string::npos;

  is_option = contains_space && (contains_p || contains_c);

  std::cerr << a->name << " is_option: " << is_option << std::endl;
  best_bid = 0;
  maxprice = a->maxpx;
  incr = a->bo_spread;
  ASSERT(maxprice - 1 > 0, "max price too low");
  best_ask = uint(maxprice - 1);
  for (int j = 0; j <= maxprice + 1; ++j)
  {
    bidqs.push_back(new OrderQ());
    askqs.push_back(new OrderQ());
  }

  //index_of_darr_test();

  tx_px_3.set_capacity(3);
  tx_px_5.set_capacity(5);
  tx_px_10.set_capacity(10);
  tx_px_15.set_capacity(15);
  tx_px_20.set_capacity(20);
  tx_px_25.set_capacity(25);
  tx_px_30.set_capacity(30);
  tx_px_50.set_capacity(50);
  tx_px_100.set_capacity(100);
  tx_px_200.set_capacity(200);
  tx_px_500.set_capacity(500);

  tx_sz_3.set_capacity(3);
  tx_sz_5.set_capacity(5);
  tx_sz_10.set_capacity(10);
  tx_sz_15.set_capacity(15);
  tx_sz_20.set_capacity(20);
  tx_sz_25.set_capacity(25);
  tx_sz_30.set_capacity(30);
  tx_sz_50.set_capacity(50);
  tx_sz_100.set_capacity(100);
  tx_sz_200.set_capacity(200);
  tx_sz_500.set_capacity(500);

  if (pt.size())
  {
    SNGH;
    // has_pred=true;
    // auto models = create_models(pt);
    // up=static_cast<mid8_prob *>(models.at("next_up"));
    // up2=static_cast<move_2 *>(models.at("move_2"));
    // mid_calc=static_cast<mid8_calc *>(models.at("mid_calc"));
    // vwap1=static_cast<mid8_calc *>(models.at("vwap1"));
    // vwap2=static_cast<mid8_calc *>(models.at("vwap2"));
    // vol=static_cast<vol_calc *>(models.at("vol"));
  }
  else
  {
    has_pred = false;
  }
}

void act::OB::get_handler(const frame::cons::msg::Get *msg) noexcept
{
  if (msg->what == "bbbo")
  {
    auto rep = (boost::format("BBBO: %d %d") % best_bid % best_ask).str();
    reply(new frame::cons::msg::Page(rep));
  }
  else if (msg->what == "qat")
  {
    // What rests at one price level: real size, real order count, and our own
    // size. Our own is deliberately NOT visible
    // through "bbbo": sim orders are excluded from the BBO (process_add_or_mod
    // bails early, and the BBO walk uses isempty_or_allsim) so that a strategy
    // cannot react to its own quote. That makes this the only way to ask
    // whether one of our orders has actually reached the book yet -- which is
    // exactly the question OB's delay queue exists to answer, and which was
    // unanswerable, and so untested, until now.
    //
    //   kv["side"] = "B" | "S", kv["px"] = price in ticks
    // reply: "QAT: <real size> <real order count> <our size>", or -1s when the
    // level is out of range or the arguments do not parse.
    long sz = -1, cnt = -1, simsz = -1;
    auto sideit = msg->kv.find("side");
    auto pxit   = msg->kv.find("px");
    if (sideit != msg->kv.end() && pxit != msg->kv.end() &&
        (sideit->second == "B" || sideit->second == "S"))
    {
      // strtol, not stoi: kv comes straight from console tokens and this
      // handler is noexcept, so a throw here would terminate the process
      // rather than reach the console's own catch.
      char *end = nullptr;
      const long px = std::strtol(pxit->second.c_str(), &end, 10);
      const bool parsed = end && *end == '\0' && end != pxit->second.c_str();
      const auto &qv = (sideit->second == "B") ? bidqs : askqs;
      if (parsed && px > 0 && px < long(qv.size()) && qv[px])
      {
        sz    = qv[px]->get_orders_in_book();
        cnt   = qv[px]->get_size_of_book_cnt();
        simsz = qv[px]->get_sim_in_book();
      }
    }
    reply(new frame::cons::msg::Page(
        (boost::format("QAT: %d %d %d") % sz % cnt % simsz).str()));
  }
}

void act::OB::start_handler(const actors::msg::Start *) noexcept
{
  log_inf("start");
  if (man)
  {
    // Get venue from asset and look up SOM by venue name
    auto a = ref::RefData::inst().get_asset(sym);
    if (a && a->is_exchange_md_set())
    {
      std::cerr << "OB::start_handler sym=" << sym << " asset=" << a->name
                << " exchange_md=" << en::to_string(a->get_exchange_md())
                << " exchange_md_str=" << a->exchange_md_str
                << " exchange_md_val=" << a->get_exchange_md().value << std::endl;
      std::string som_name = std::string("SOM_") + en::to_string(a->get_exchange_md());
      som = man->get_actor_by_name(som_name);
    }
  }
}

void act::OB::clear()
{
  log_inf("act::OB::clear() called");
  ordermap.clear();
  for (const auto &s : lowprio_datasubs)
  {
    s->send(new msg::Clear(sym), this);
  }
  for (const auto &s : hiprio_datasubs)
  {
    s->fast_send(new msg::Clear(sym), this);
  }
  for (std::size_t i = 0; i < bidqs.size(); i++)
  {
    auto q = bidqs[i];
    if (q->isempty())
      continue;
    q->canc_notify_all(&ret_path_);
  }
  for (std::size_t i = 0; i < askqs.size(); i++)
  {
    auto q = askqs[i];
    if (q->isempty())
      continue;
    q->canc_notify_all(&ret_path_);
  }
  best_bid = 0;
  best_ask = ref::RefData::inst().get_asset(sym)->maxpx - 1;
  for (std::size_t i = 0; i < bidqs.size(); i++)
  {
    auto q = bidqs[i];
    if (!q->isempty())
    {
      std::cerr << i << " not empty ";
    }
    ASSERT(q->isempty(), "not empty");
    ASSERT(!q->get_orders_in_book(), "orders");
  }
  for (std::size_t i = 0; i < askqs.size(); i++)
  {
    auto q = askqs[i];
    if (!q->isempty())
    {
      std::cerr << i << " not empty ";
    }
    ASSERT(q->isempty(), "not empty");
    ASSERT(!q->get_orders_in_book(), "orders");
  }
  log_inf("clear done");
}

act::OB::~OB()
{
  FOR(auto p, bidqs)
  delete p;
  FOR(auto p, askqs)
  delete p;
}

void act::OB::add(
    uint64_t txtim,
    unsigned long long id,
    en::bs side, int px, int sz,
    actors::Actor *sender, en::ot ot,
    int owner,
    uint64_t exordid,
    en::x venue) noexcept
{

  uint64_t sim_tim=0;

  bool sim = mda::OrderID::exchid(id) == en::x::SIM;

  if (sim)
  {
    log_dbg("have sim order add");
    ASSERT(sender, "sim orders must have a sender");

    sim_tim = currtim;
    ASSERT(sim_tim>0, "invalid sim time set");

#ifdef TRACEORDERS
    // debug = true;
    cerr << ">>> ADD: " << mda::OrderID::id(id) << " " << px << " " << sz << " "
         << en::to_string(side) << " "
         << sim_tim.date << " " << sim_tim.ns
         << " " << prev_xoid
         << endl;
#endif
  }
  auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
  ASSERT(maxprice > 0, "maxprice");
  if (px >= maxprice)
  {
    log_inf("price: %d over max: %d on sym: %d", px, this->maxprice, sym);
    return;
  }

  if (debug)
  {
    auto ctim = chutil::Time::from_epoch(txtim);
    OBFILE << boost::format("OBX: %s ADD, %s, date: %d, mstim: %d, epoch: %d, id: %d, ordid: %d, exordid: %d, exchid: %s, sym: %d, side: %s, px: %d, sz: %d") %
                  get_name() %
                  ctim.to_string() %
                  ctim.date %
                  ctim.ns %
                  ctim.to_epoch_utc() %
                  id %
                  mda::OrderID::id(id) %
                  exordid %
                  en::to_string(mda::OrderID::exchid(id)) %
                  sym %
                  en::to_string(side) %
                  px %
                  sz
           << std::endl;
  }

  log_trc("add date: %d, nstim: %d, id: %d, ordid: %d, exchid: %s, sym: %d, side: %s, px: %d, sz: %d",
          ctim.date,
          ctim.ns,
          id,
          mda::OrderID::id(id),
          en::to_string(mda::OrderID::exchid(id)),
          sym,
          en::to_string(side),
          px,
          sz);

  qvec_t *qv = 0;
  if (side == en::bs::BUY)
  {
    if (debug && sim)
      log_dbg("sim buy order");
    qv = &bidqs;
  }
  else if (side == en::bs::SEL)
  {
    if (debug && sim)
      log_dbg("sim sel order");
    qv = &askqs;
  }
  else
    SNGH;

  if (sz <= 0)
  {
    log_err("add order has bad size %d %d %d", sym, id, sz);
    SNGH;
  }

  auto fill_sim_on_arrival = [this, txtim, id, side, sz, sender, owner, px, ot, sim, exordid]() -> bool
  {
    ASSERT(sim, "must be sim");
    log_dbg("sim add order %d %d %d %d", sym, id, sz, px);
    ASSERT(en::is_valid(ot), "invalid ot");
    /// *** ASSUMPTION ***
    // if a sim bid is placed > best ask or
    // sim ask is placed < best bid
    // fill immediately
    ASSERT(ot == en::ot::LIMIT, "order type not implemented");
    if (ot == en::ot::LIMIT)
    {
      // returns false on reject
      // partial fills not supported
      [[maybe_unused]]
      auto record_aggr_fills = [](OrderQ *q, int sz_to_fill) noexcept
      {
        auto ord = q->get_head();
        ASSERT(ord, "empty q");
        while (true)
        {
          while (ord->get_sz() <= ord->aggr_fills)
          {
            if (ord->next)
              ord = static_cast<frame::ob::Order *>(ord->next);
            else
            {
              return false;
            }
          }
          //
          // a reject needs to be sent as it may not be possible
          // to prevent all aggressive orders to be generated
          //
          auto remaining_to_fill = ord->get_sz() - ord->aggr_fills;
          ASSERT(remaining_to_fill >= 0, "rem to fill");
          if (remaining_to_fill >= sz_to_fill)
          {
            ord->aggr_fills += sz_to_fill;
            break;
          }
          else
          {
            ord->aggr_fills += remaining_to_fill;
            ASSERT(ord->aggr_fills == ord->get_sz(), "bad aggr fills");
            sz_to_fill -= remaining_to_fill;
            ASSERT(sz_to_fill > 0, "sz_to_fill");
          }
        }
        return true;
      };
      if (side == en::bs::BUY)
      {
        if ((px >= best_ask) * (best_bid < best_ask))
        {
          log_inf("sim bid>=best_ask id: %d, id: %d, sym: %d px: %d, best_bid: %d, best_ask: %d", id, mda::OrderID::id(id), sym, px, best_bid, best_ask);
          // generate a fill
          // *** ASSUMPTION ***
          // NOT ALLOWING TRADE THROUGH THE STACK ONLY ON THE FIRST LEVEL
          [[maybe_unused]] auto q_i = askqs[best_ask];
          [[maybe_unused]] auto short_id = frame::mda::OrderID::id(id);
#ifdef ALLOWREJONARR
          auto rc = record_aggr_fills(q_i, sz);
          if (!rc)
          {
            auto rej_msg = new frame::som::msg::Reject(short_id, som::msg::Reject::TOOMANYAGGR);
            publish_delayed(sender, rej_msg, last_processed_ts);
#ifdef TRACEORDERS
            cerr << ">>> REJONARR: " << short_id << " " << sim_tim.date << " " << sim_tim.ns << endl;
#endif
            return true;
          }
#endif
          auto o = new Order(txtim, id, exordid, sym, side, sz, sender, owner);
          OrderQ::fill_notify_s(o, sz, -1, best_ask, en::mt::EXECD, txtim, &ret_path_);
#ifdef TRACEORDERS
          cerr << ">>> FILLONARR: " << short_id << " " << sim_tim.date << " " << sim_tim.ns << endl;
#endif
          delete o;
          o = nullptr;
          return true;
        }
      }
      else if ((side == en::bs::SEL))
      {
        if ((px <= best_bid) * (best_bid < best_ask))
        {
          log_inf("sim ask<=best_bid id: %d , id: %d, sym: %d px: %d, best_bid: %d, best_ask: %d", id, mda::OrderID::id(id), sym, px, best_bid, best_ask);
          // generate a fill
          // *** ASSUMPTION ***
          // NOT ALLOWING TRADE THROUGH THE STACK ONLY ON THE FIRST LEVEL
          [[maybe_unused]] auto q_i = bidqs[best_bid];
          [[maybe_unused]] auto short_id = frame::mda::OrderID::id(id);
#ifdef ALLOWREJONARR
          auto rc = record_aggr_fills(q_i, sz);
          if (!rc)
          {
            auto rej_msg = new frame::som::msg::Reject(short_id, som::msg::Reject::TOOMANYAGGR);
#ifdef TRACEORDERS
            cerr << ">>> REJONARR: " << short_id << " " << sim_tim.date << " " << sim_tim.ns << endl;
#endif
            publish_delayed(sender, rej_msg, last_processed_ts);
            return true;
          }
#endif
          auto o = new Order(txtim, id, exordid, sym, side, sz, sender, owner);
          OrderQ::fill_notify_s(o, sz, -1, best_bid, en::mt::EXECD, txtim, &ret_path_);
#ifdef TRACEORDERS
          cerr << ">>> FILLONARR: " << short_id << " " << sim_tim.date << " " << sim_tim.ns << endl;
#endif
          delete o;
          o = nullptr;
          return true;
        }
      }
      else
      {
        cerr << "fill_sim_on_arrival id: " << mda::OrderID::id(id)
             << " side: " << side << " px: " << px
             << " bb: " << best_bid << " ba: " << best_ask
             << endl;

        SNGH;
      }
    }
    return false;
  };

#define FILLSTRAYSIM
#ifdef FILLSTRAYSIM

  //
  // this is called on an add on a real order
  //
  // venue is captured only because the uncross branch below passes it to
  // do_canc and the lambda must compile -- do_canc's parameter is
  // [[maybe_unused]] en::x mkt and is never read, so the cancel is NOT
  // venue-routed. The NOXMKT code used venue without capturing it, which never
  // showed up because the block could not be compiled in any build. That it
  // did not compile is also the reason not to assume the rest of that block is
  // sound merely because it is old.
  // Set by the uncross below, read by check_for_bo_change. The inside only has
  // to move if we actually emptied levels; walking on EVERY add is what made
  // this a hot-path problem (an empty contra side after a channel reset marched
  // best_ask across the whole ~25000-level ladder, calling isempty_or_allsim --
  // itself a list walk -- on each one, inside a single market-data record).
  int uncrossed_here = 0;

  auto fill_stray_sim_orders = [this, px, side, maxprice, txtim, venue,
                                &uncrossed_here]() noexcept
  {
    //
    // fill stray sim orders when a real add bid arrives but there are sim
    // orders that are lower in price
    //
    // The widened range is a LIVE REPAIR and is gated on !do_cross_check, so
    // that simulation keeps exactly the fill model it had.
    //
    // `px >= best_ask` and `px > best_bid` agree while the book is healthy
    // (best_bid < best_ask), so on a healthy book this changes nothing either
    // way. They differ in two places: a book that arrives ALREADY crossed --
    // mod_order_that_is_not_found leaves the BBO pinned to a level it never
    // emptied, by its own comment, and the levels in [best_ask, best_bid) were
    // then never visited -- and a merely LOCKED book, best_bid == best_ask,
    // where a real add AT the lock satisfies `px >= best_ask` but not
    // `px > best_bid`.
    //
    // A lock is legal everywhere else in this file: the invariant is
    // `best_bid <= best_ask`, cross_check has a non-fatal branch for it and
    // fill_sim_on_arrival refuses to fill unless `best_bid < best_ask`. Letting
    // the widened test reach the fill() branch would therefore have handed a
    // full fill to every resting shadow SELL at a locked price -- fills the old
    // test produced none of, on exactly the disorderly moments the corpus is
    // measuring. Every VWAP and participation number in the corpus would have
    // moved, while the note at the head of add() claims simulation is unchanged.
    const bool repair = !do_cross_check;
    if ((side == en::bs::BUY) * ((px > best_bid) + (repair * (px >= best_ask))))
    {
      int tmp_px = repair ? std::min(best_bid, best_ask) : best_bid;
      while (tmp_px <= px)
      {
        // Bounds-check BEFORE the read, as the SEL branch below does. The only
        // other bound is `tmp_px >= maxprice` at the foot of the loop, which
        // cannot protect the first access -- and `maxprice` is re-read from
        // RefData on every add while askqs was sized from the ctor snapshot, so
        // it is not the container's bound anyway. .at() throwing inside a
        // lambda declared noexcept is std::terminate, with no diagnostic.
        if (uint(tmp_px) >= askqs.size())
        {
          std::cerr << name << " tmp_px: " << tmp_px << " askqs.size(): " << askqs.size() << std::endl;
          ERR("tmp_px out of range, check universe file and increase maxpx");
        }
        auto q_i = askqs.at(tmp_px);
        ASSERT(q_i, "no q");
        auto stray_o = q_i->get_head();
        while (stray_o)
        {
          auto next = static_cast<frame::ob::Order *>(stray_o->next);
          if ((stray_o->issim()) * (stray_o->get_side() == en::bs::SEL))
          {
            //
            // a sim offer has price less than or equal to a real bid
            //
            log_inf("filling SEL stray sim order id: %d", stray_o->get_id());
            fill(q_i, stray_o, stray_o->get_sz(), tmp_px, txtim);
#ifdef TRACEORDERS
            std::cerr << ">>> FILLSTRAY: " << mda::OrderID::id(stray_o->get_id()) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
          }
          // Uncross, live only. See the note at the head of add().
          else if (!do_cross_check && !stray_o->issim() &&
                   stray_o->get_side() == en::bs::SEL)
          {
            // Read the exchange id BEFORE the cancel: do_canc -> canc_notify ->
            // remove_order deletes the Order, so stray_o is dangling after it.
            const uint64_t xoid = stray_o->get_exordid();
            const int      xsz  = stray_o->get_sz();
            do_canc(q_i, stray_o, tmp_px, xsz, 0, en::mt::CANCD, venue);

            // OB::ordermap keeps its entry for this order, deliberately.
            //
            // The entry is keyed by the exchange orderID and outlives the order
            // we just destroyed, but nothing ever acts on it again:
            //
            //  - the exchange's own DELETE finds it, builds a CANCD for a chopid
            //    that is no longer in the level's qordermap, and mod() takes
            //    mod_order_that_is_not_found -> non-sim branch -> return. The
            //    "skips the BBO re-walk" hazard in that path needs the order to
            //    still be RESTING; this one is gone from the book and the inside
            //    was re-walked here, at cancel time.
            //  - a re-ADD of the same orderID cannot reach the "order added
            //    twice" guard, because the only thing that re-sends an ADD is a
            //    recovery, and a recovery calls clear(), which begins with
            //    ordermap.clear().
            //
            // So an erase here would buy nothing -- and doing it at this point
            // would be actively wrong: this runs inside add(), which
            // data_handler reaches through process_q() while holding a raw
            // order_info_t* into ordermap (:2103, written through at :2191).
            // ordermap is a std::flat_map, so erase() shifts later elements and
            // invalidates that pointer.

            ++num_cross_recover;
            ++uncrossed_here;

            // Name what was deleted. This is the one place the book destroys a
            // REAL exchange order, so the shutdown banner's "prices around those
            // events are not trustworthy" needs a time, a price and an id to
            // point at -- TRACEORDERS is defined nowhere in the tree, so without
            // this the event left no trace at all.
            log_err("%s UNCROSS: cancelled real %s exordid=%llu sz=%d at px=%d "
                    "(bid %d ask %d, incoming %s px=%d) -- prices here are suspect",
                    get_name(), (side == en::bs::BUY ? "ASK" : "BID"),
                    (unsigned long long)xoid, xsz, tmp_px, best_bid, best_ask,
                    en::to_string(side), px);
#ifdef TRACEORDERS
            std::cerr << ">>> CANCSTRAY: " << mda::OrderID::id(stray_o->get_id()) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
          }
          stray_o = next;
        }
        tmp_px += 1;
        if (tmp_px >= maxprice)
          break;
      }
    }
    //
    // fill stray sim orders when a real ask arrives but there are sim orders
    // that are higher in price
    //
    else if ((side == en::bs::SEL) * ((px < best_ask) + (repair * (px <= best_bid))))
    {
      int tmp_px = repair ? std::max(best_ask, best_bid) : best_ask;
      while (tmp_px >= px)
      {
        if (uint(tmp_px) >= bidqs.size())
        {
          std::cerr << name << " tmp_px: " << tmp_px << " bidqs.size(): " << bidqs.size() << std::endl;
          ERR("tmp_px out of range, check universe file and increase maxpx");
        }
        auto q_i = bidqs.at(tmp_px);
        ASSERT(q_i, "no q_i");
        auto stray_o = q_i->get_head();
        while (stray_o)
        {
          auto next = static_cast<frame::ob::Order *>(stray_o->next);
          if ((stray_o->issim()) * (stray_o->get_side() == en::bs::BUY))
          {
            //
            // a sim bid has a price lower than a real ask
            //
            log_inf("filling BUY stray sim order id: %d", stray_o->get_id());
            fill(q_i, stray_o, stray_o->get_sz(), tmp_px, txtim);
#ifdef TRACEORDERS
            std::cerr << ">>> FILLSTRAY: " << mda::OrderID::id(stray_o->get_id()) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
          }
          // Uncross, live only. See the note at the head of add().
          else if (!do_cross_check && !stray_o->issim() &&
                   stray_o->get_side() == en::bs::BUY)
          {
            // Read the exchange id BEFORE the cancel: do_canc -> canc_notify ->
            // remove_order deletes the Order, so stray_o is dangling after it.
            const uint64_t xoid = stray_o->get_exordid();
            const int      xsz  = stray_o->get_sz();
            do_canc(q_i, stray_o, tmp_px, xsz, 0, en::mt::CANCD, venue);

            // OB::ordermap keeps its entry for this order, deliberately.
            //
            // The entry is keyed by the exchange orderID and outlives the order
            // we just destroyed, but nothing ever acts on it again:
            //
            //  - the exchange's own DELETE finds it, builds a CANCD for a chopid
            //    that is no longer in the level's qordermap, and mod() takes
            //    mod_order_that_is_not_found -> non-sim branch -> return. The
            //    "skips the BBO re-walk" hazard in that path needs the order to
            //    still be RESTING; this one is gone from the book and the inside
            //    was re-walked here, at cancel time.
            //  - a re-ADD of the same orderID cannot reach the "order added
            //    twice" guard, because the only thing that re-sends an ADD is a
            //    recovery, and a recovery calls clear(), which begins with
            //    ordermap.clear().
            //
            // So an erase here would buy nothing -- and doing it at this point
            // would be actively wrong: this runs inside add(), which
            // data_handler reaches through process_q() while holding a raw
            // order_info_t* into ordermap (:2103, written through at :2191).
            // ordermap is a std::flat_map, so erase() shifts later elements and
            // invalidates that pointer.

            ++num_cross_recover;
            ++uncrossed_here;

            // Name what was deleted. This is the one place the book destroys a
            // REAL exchange order, so the shutdown banner's "prices around those
            // events are not trustworthy" needs a time, a price and an id to
            // point at -- TRACEORDERS is defined nowhere in the tree, so without
            // this the event left no trace at all.
            log_err("%s UNCROSS: cancelled real %s exordid=%llu sz=%d at px=%d "
                    "(bid %d ask %d, incoming %s px=%d) -- prices here are suspect",
                    get_name(), (side == en::bs::BUY ? "ASK" : "BID"),
                    (unsigned long long)xoid, xsz, tmp_px, best_bid, best_ask,
                    en::to_string(side), px);
#ifdef TRACEORDERS
            std::cerr << ">>> CANCSTRAY: " << mda::OrderID::id(stray_o->get_id()) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
          }
          stray_o = next;
        }
        tmp_px -= 1;
        if (tmp_px <= 0)
          break;
      }
    }
  };

#endif

  auto check_for_bo_change = [this, side, px, txtim, sim, &uncrossed_here](en::x venue)
  {
    ASSERT(!sim, "cant be sim");

    // bochg: the ASK side moved. bbchg: the BID side moved. They were both
    // being set by whichever branch ran, which left bbchg permanently false and
    // the BUY notification below unreachable.
    bool bochg = false, bbchg = false;
    // Set false only by a walk that ran off the end of the ladder without
    // finding a real order -- there is then no inside to publish on that side.
    bool ask_is_real = true, bid_is_real = true;

    if (side == en::bs::BUY)
    {
      // best for this x
      // if (px > best_bid_x[venue])
      // {
      //   best_bid_x[venue] = px;
      // }
      // overall best bid
      if (px > best_bid)
      {
        // its greater than bb
        best_bid = px;
        bbchg = true;
      }
      // The uncross above emptied the ask levels this bid crossed, so the
      // inside has to move past them. Same loop mod() runs after a delete --
      // including setting the change flag, without which the move is never
      // published.
      //
      // The condition is `still crossed`, NOT `we cancelled something`.
      // uncrossed_here only counts REAL contra orders cancelled, and the case
      // this repair exists for is precisely the one where there are none:
      // mod_order_that_is_not_found leaves best_ask pinned to a level whose
      // orders were already dropped, so the level is EMPTY. The walk was then
      // skipped while `best_bid = px` had already been set, and add() returned
      // with the book still crossed -- straight into the invariant below. A
      // crossed range holding only sim orders had the same hole.
      //
      // Still guarded, so the hot path is untouched: on a healthy book, and on
      // a drained ask side after a channel reset (best_ask sits at the top of
      // the ladder, so best_bid >= best_ask is false), this does not run. That
      // was the ~25000-level march the guard was introduced for.
      //
      // Bounded by askqs.size(), not by `maxprice`: the local captured above is
      // refreshed from RefData at runtime while the queues were sized from the
      // ctor's snapshot, and BFA rewrites Asset::maxpx on a security definition
      // (treasuries take `int(180/unit)`). Using the container's own bound is
      // the only one that cannot walk off the end -- and .at() inside a noexcept
      // lambda would be std::terminate, not a catchable throw.
      // LIVE ONLY, like every other part of the uncross. In simulation a
      // crossed book must reach the invariant in process_market_data and stop
      // the run -- silently repairing the inside there would hide exactly the
      // reconstruction fault the replay exists to catch, and every fill after
      // it would be fiction. uncrossed_here can only be non-zero when
      // !do_cross_check in any case; the explicit test is so that the second
      // condition cannot quietly change simulation behaviour.
      if (!do_cross_check && (uncrossed_here || best_bid >= best_ask))
      {
        const int hi = int(askqs.size()) - 1;
        // Bound BEFORE the first read: best_ask is re-seeded by clear() from
        // the RUNTIME maxpx, which BFA can raise past the size askqs was built
        // with, and a read there is a virtual call through garbage.
        if (best_ask < 0 || best_ask > hi)
          ERR("best_ask out of range for askqs, increase maxpx in the universe file");
        auto q = askqs[best_ask];
        while (q->isempty_or_allsim() && best_ask < hi)
        {
          best_ask++;
          q = askqs[best_ask];
          bochg = true;
        }
        // Walked the whole ladder without finding a real order: there is no ask
        // to publish. Saying so beats publishing the top of the ladder, which
        // passes every guard below -- it is positive and above the bid -- and
        // reaches the lights as a 25,000-tick spread they price against.
        ask_is_real = !q->isempty_or_allsim();
      }
    }
    else if (side == en::bs::SEL)
    {
      // best ask for this x
      // if (px < best_ask_x[venue])
      // {
      //   best_ask_x[venue] = px;
      // }
      // overall best ask
      if (px < best_ask)
      {
        // its less than best ask
        best_ask = px;
        bochg = true;
      }
      if (!do_cross_check && (uncrossed_here || best_bid >= best_ask))
      {
        // `> 1`, as mod() has it: stopping at 0 lets a bid price of 0 be
        // published as the inside.
        if (best_bid < 0 || uint(best_bid) >= bidqs.size())
          ERR("best_bid out of range for bidqs, increase maxpx in the universe file");
        auto q = bidqs[best_bid];
        while (q->isempty_or_allsim() && best_bid > 1)
        {
          best_bid--;
          q = bidqs[best_bid];
          bbchg = true;
        }
        // Same as the ask side: stopping at 1 with nothing there is "no bid",
        // not "the bid is one tick". Publishing it marks every long position at
        // a tick and passes the best_bid > 0 guard below.
        bid_is_real = !q->isempty_or_allsim();
      }
    }
    else
      SNGH;

    if (bbchg + bochg)
    {
      // AND, as mod() has it (`* (best_bid > 0) * (best_ask > 0)`). The `+`
      // here was an OR, so a BBO with best_bid == 0 -- a fully drained bid side
      // -- was publishable, reaching the lights as a spread of ask minus zero.
      if ((best_bid > 0) * (best_ask > 0) * ask_is_real * bid_is_real)
      {
        if (bbchg)
          notifybbbosubs(txtim, en::bs::BUY, venue);
        else if (bochg)
          notifybbbosubs(txtim, en::bs::SEL, venue);
        else
          SNGH;
      }
    }
  };

  if (sim)
  {
    if (fill_sim_on_arrival())
    {
#ifdef TRACEORDERS
      std::cerr << ">>> FILLONARRIV: " << mda::OrderID::id(id) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
      return;
    }
  }

  // adjust bbbo unless it is a sim order
  if (!sim)
  {
#ifdef FILLSTRAYSIM
    fill_stray_sim_orders();
#endif
    check_for_bo_change(venue);
  }

  //
  // the following is good for sim and real orders
  //
  auto q = (*qv)[px];
  ASSERTF(q, boost::format("no q at px %d") % px);
  auto o = new Order(txtim, id, exordid, sym, side, sz, sender, owner);
  q->append(o);

  if (debug)
    debug_print_book(id, txtim, 'A', en::mt::NONE, side, px, sz);
  check_bbbo();
}

// full/partial cancel or order executed
void act::OB::mod(
    actors::Actor *sender,
    uint64_t txtim,
    unsigned long long id,
    en::mt modtyp,
    en::bs side,
    int px,
    int sz,
    int disp_sz,
    en::x mkt,
    uint64_t exordid) noexcept
{
  ASSERT(id > 0, "bad oid");
  ASSERT(sz < CHOPIN_MAX_ORD_SZ, "sz too large");
  if (modtyp != en::mt::CANCD && sz < 0)
  {
    cerr << txtim << " " << exordid
         << " size is 0 " << sz << endl;
    SNGH;
  }
  // auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
  ASSERT(maxprice > 0, "maxprice");

  if (px >= maxprice)
  {
    log_inf("price: %d over max on sym: %d", px, sym);
    return;
  }
  bool sim = mda::OrderID::exchid(id) == en::x::SIM;
  if (sim)
  {
#ifdef TRACEORDERS
    std::cerr << ">>> MOD: " << mda::OrderID::id(id) << " " << en::to_string(modtyp) << " " << ctim.date << " " << ctim.ns
              << " " << prev_xoid
              << std::endl;
#endif
    ASSERT(sender, "mod sim must have sender");

    log_dbg(">>> MOD: id: %d %s", mda::OrderID::id(id), en::to_string(modtyp));
  }

  if (debug)
  {
    chutil::Time ctim = chutil::Time::from_epoch(txtim);
    OBFILE << boost::format("OBX: %s %s, MOD, date: %d, nstim: %d, epoch: %d, id: %d, ordid: %d, exordid: %d, exchid: %s, sym: %d, side: %s, px: %d, sz: %d, dispsz: %d, modtyp: %s") %
                  get_name() %
                  ctim.to_string() %
                  ctim.date %
                  ctim.ns %
                  ctim.to_epoch_utc() %
                  id %
                  mda::OrderID::id(id) %
                  exordid %
                  en::to_string(mda::OrderID::exchid(id)) %
                  sym %
                  en::to_string(side) %
                  px %
                  sz %
                  disp_sz %
                  en::to_string(modtyp)
           << std::endl;
  }

  log_trc("mod tim: %d, id: %d, exchid: %s, ordid: %d, sym: %d, typ: %s, side: %s, px: %d, sz: %d, disp_sz: %d",
          ctim.ns,
          id, en::to_string(mda::OrderID::exchid(id)), mda::OrderID::id(id),
          sym, en::to_string(modtyp), en::to_string(side), px, sz, disp_sz);

  qvec_t *qv = 0;
  bool bbchg = false;
  bool bochg = false;
  Order *o = 0;
  // find order
  if (side == en::bs::BUY)
  {
    if (debug && sim)
      log_dbg("bid q");
    qv = &bidqs;
  }
  else if (side == en::bs::SEL)
  {
    if (debug && sim)
      log_dbg("ask q");
    qv = &askqs;
  }
  else
    SNGH;

  auto q = (*qv)[px];

  Order *orderptr = 0;
  bool found_order = false;
  auto ptr_ = q->qordermap.find(id);
  if (ptr_ != q->qordermap.end())
  {
    found_order = true;
    orderptr = ptr_->second;
  }

  //bool found_order = q->ordermap.get(id, orderptr);

  auto mod_order_that_is_not_found = [sim, this, modtyp, sender, id, px, side, exordid]()
  {
    if (sim)
    {
      if ((modtyp == en::mt::CANC) + (modtyp == en::mt::CANCD))
      {
        log_trc("got mod but sim order not found -- sending canc reject id: %d, order id: %d",
                id,
                mda::OrderID::id(id));
        auto f =
            new som::msg::CancReject(
                mda::OrderID::id(id),
                som::msg::CancReject::NOTFOUND);
        ASSERT(sender, "sim cancs must have sender");
        // An exchange event like any other, so it pays the SAME inbound hop as
        // the Fill it races. An order that filled on arrival is gone from the
        // book, so a cancel for it lands here -- and the fill and this reject
        // are about the same order. Sending this inline while the fill waited
        // on pub_q let the reject overtake it: the light took the reject,
        // cleared its slot, placed again, and then the fill for the previous
        // order arrived ("fill for wrong order id"), stranding the new order in
        // QCoord until an add at that price tripped "already have this mm id".
        publish_delayed(sender, f, last_processed_ts);
      }
      else
      {
        if (debug)
          log_err("got mod but order %d not found on exec", id);
        SNGH;
      }
      return;
    }
    else
    {

      // the following is not an error in simulation where
      // data may not be complete but in real life it should
      // not be happening ... however locked markets are still possible

      ASSERT(modtyp != en::mt::EXECD, "no execd allowed here")

      if (modtyp == en::mt::EXEC)
      {
        // #define CHECKUNKONWMOD
#ifdef CHECKUNKONWMOD
        std::cerr << boost::format{"OBX ERR: got mod %s but order not found sym: %d id: %d id: %d"} % en::to_string(modtyp) % sym % id % mda::OrderID::id(id) << std::endl;

        ERR("ot mod for order that is not found");
#else
        return;
#endif
      }

      log_trc("got mod %s but order not found sym: %d id: %d id: %d", en::to_string(modtyp), sym, id, mda::OrderID::id(id));

      // A real cancel/modify we cannot match is how the book goes stale: the
      // order stays resting AND the early return below skips the best_bid /
      // best_ask re-walk, so the BBO stays pinned to a level we never emptied.
      // log_trc above is compiled out unless -DTRACE, which is why this went
      // unseen — report it properly, and say WHERE the order actually is so we
      // can tell a dropped message from a price mismatch.
      if (debug)
      {
        int found_at = -1;
        char found_side = '?';
        auto scan = [&](const qvec_t &v, char sd) {
          if (found_at >= 0) return;
          for (size_t lvl = 0; lvl < v.size(); ++lvl)
            if (v[lvl] && v[lvl]->qordermap.count(id)) { found_at = int(lvl); found_side = sd; return; }
        };
        scan(bidqs, 'B');
        scan(askqs, 'S');

        log_err("STALE-RISK %s: %s for id %d (exordid %d) not found at px %d side %s; "
                "order is %s%d — BBO re-walk skipped, best_bid %d best_ask %d",
                get_name(), en::to_string(modtyp), id, int(exordid), px,
                en::to_string(side),
                found_at >= 0 ? "resting elsewhere: side/level " : "not in the book at all ",
                found_at, best_bid, best_ask);
        OBFILE << "STALE-RISK " << get_name() << " " << en::to_string(modtyp)
               << " id=" << id << " exordid=" << exordid
               << " msg_px=" << px << " side=" << en::to_string(side)
               << " found_at_level=" << found_at << " found_side=" << found_side
               << " best_bid=" << best_bid << " best_ask=" << best_ask << "\n";
      }

      return;
    }
  };

  if (!found_order)
  {
    // this can happen in simulation but not for regular orders
    mod_order_that_is_not_found();
    return;
  }

  // order was found
  o = orderptr;
  if (modtyp == en::mt::EXEC)
    ASSERT(sz > 0, "bad size");
  ASSERT(o->issim() == sim, "sim not set properly");

  auto do_exec = [this, o, sz, px, q, modtyp, side, txtim]()
  {
  //
  // this for filling sim orders only no book adjustments
  //

#ifdef FLAGNOTATBESTX
    if (side == en::bs::BUY)
    {
      if (px != best_bid)
      {
        auto f = boost::format("OBX ERR: have hit not at best bid: %d, px: %d") % best_bid % px;
        cerr << f << endl;
        log_err(f.str());
      }
    }
    else
    {
      if (px != best_ask)
      {
        auto f = boost::format("OBX ERR: have tak not at best ask: %d, px: %d") % best_ask % px;
        cerr << f << endl;
        log_err(f.str());
      }
    }
#endif

    ASSERT(!o->issim(), "sim orders cannot execute");

    auto fill_prev_sim_order = [this, o, px, sz, modtyp, q, txtim]()
    {
      // fill all sim orders preceding this one
      auto prev_order = static_cast<Order *>(o->prev);

      if ((prev_order) && (prev_order->issim()))
      {
        // fill simulation roders recursively
        log_dbg("filling sim order lid: %d, oid: %d", prev_order->get_id(),
                mda::OrderID::id(prev_order->get_id()));
        if (modtyp == en::mt::EXECD)
        {
          ERR("invalid modtyp");
          fill(q, prev_order, o->get_sz(), px, txtim);
        }
        else
        {
          ASSERT(modtyp == en::mt::EXEC, "must be exec");
          fill(q, prev_order, sz, px, txtim);
        }
      }
    };

    //
    // we had a trade at the bid at price px
    // if there are any sim bids higher in price fill them
    //

    auto fill_stray_sim_orders_buy = []()
    {
#ifdef DONOTODOTHISIFDIFFERENTPRICE
      auto tmp_px = px + 1;
      while (tmp_px <= best_ask + 32)
      {
        auto q_i = bidqs[tmp_px];
        auto stray_o = q_i->get_head();
        while (stray_o)
        {
          auto next = static_cast<frame::ob::Order *>(stray_o->next);
          if ((stray_o->issim()) * (stray_o->get_side() == en::bs::BUY))
            fill(q_i, stray_o, stray_o->get_sz(), tmp_px, ctim);
#ifdef FILLSTRAYREAL
          else if (!stray_o->issim() && stray_o->get_side() == en::bs::BUY)
            do_canc(q_i, stray_o, tmp_px, stray_o->get_sz(), 0, en::mt::CANCD, mkt);
#endif
          stray_o = next;
        }
        tmp_px += 1;
        if (tmp_px >= maxprice)
          break;
      }
#endif
    };

    //
    // we had a trade at the ask at price px
    // if there are any sim asks at lower price fill them
    //
    auto fill_stray_sim_orders_sel = [/*this, px, txtim*/]()
    {
#ifdef DONOTODOTHISIFDIFFERENTPRICE
      auto tmp_px = px - 1;
      while (tmp_px >= best_bid - 32)
      {
        auto q_i = askqs[tmp_px];
        auto stray_o = q_i->get_head();
        while (stray_o)
        {
          auto next = static_cast<frame::ob::Order *>(stray_o->next);
          if ((stray_o->issim()) * (stray_o->get_side() == en::bs::SEL))
            fill(q_i, stray_o, stray_o->get_sz(), tmp_px, ctim);
#ifdef FILLSTRAYREAL
          else if (!stray_o->issim() && stray_o->get_side() == en::bs::SEL)
            do_canc(q_i, stray_o, tmp_px, stray_o->get_sz(), 0, en::mt::CANCD, mkt);
#endif
          stray_o = next;
        }
        tmp_px -= 1;
        if (tmp_px <= 0)
          break;
      }
#endif
    };

    ASSERT(modtyp == en::mt::EXEC, "can only fill sims on exec");

    //
    // fill sim orders only if the executing order is at the front
    //
    if (o == q->get_head_no_sim())
    {
      fill_prev_sim_order();
      if (side == en::bs::BUY)
      {
        fill_stray_sim_orders_buy();
      }
      else if (side == en::bs::SEL)
      {
        fill_stray_sim_orders_sel();
      }
    }
  };

  ASSERT(modtyp != en::mt::EXECD, "execd not allowed here to be used in order q only");

  if (modtyp == en::mt::EXEC)
  {
    do_exec();
  }
  else if ((modtyp == en::mt::CANC) + (modtyp == en::mt::CANCD))
  {
    do_canc(q, o, px, sz, disp_sz, modtyp, mkt);
  }

  ASSERT(qv, "notset");
  if (sim)
    return; // the rest of this does not apply to sim orders

  // adjust bid and ask
  if ((side == en::bs::BUY) * (px <= best_bid))
  {
    const OrderQ *q = (*qv)[best_bid];
    while (q->isempty_or_allsim() * (best_bid > 1))
    {
      ASSERT(best_bid > 0, "0");
      best_bid -= 1;
      q = (*qv)[best_bid];
      bbchg = true;
    }
  }
  else if ((side == en::bs::SEL) * (px >= best_ask))
  {
    const OrderQ *q = (*qv)[best_ask];
    auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
    ASSERT(maxprice - 1 > 0, "maxprice");
    while (q->isempty_or_allsim() * (best_ask < maxprice - 1))
    {
      best_ask += 1;
      q = (*qv)[best_ask];
      bochg = true;
    }
  }

  if ((bbchg + bochg) * (best_bid > 0) * (best_ask > 0))
  {
    if (bbchg)
      notifybbbosubs(txtim, en::bs::BUY, mkt);
    else if (bochg)
      notifybbbosubs(txtim, en::bs::SEL, mkt);
    else
      SNGH;
  }

  if (debug)
  {
    check_bbbo();
    debug_print_book(id, txtim, 'M', modtyp, side, px, sz);
  }
}

// fill sim orders recursively
void act::OB::fill(OrderQ *q, Order *s, int sz, int px, uint64_t tim) noexcept
{
  log_dbg("filling order lid: %d, id: %d", s->get_id(), mda::OrderID::id(s->get_id()));
  int fillsz = std::min(s->get_sz(), sz);
  if (fillsz <= 0)
  {
    log_err("filling order lid: %d, id: %d, venue: %d, but it has no size s->sz: %d, sz: %d",
            s->get_id(), mda::OrderID::id(s->get_id()), mda::OrderID::exchid(s->get_id()),
            s->get_sz(), sz);
    ERR("filling for 0");
  }
  ASSERT(s->issim(), "for sim orders only");
  if (s->get_sz() <= sz) // this order is smaller than what traded
  {
    // fill this order entirely
    log_dbg("filling order entirely because o->sz: %d < sz: %d", s->get_sz(), sz);
    auto prev = s->prev;


    bool order_found = false;
    auto ptr_ = q->qordermap.find(s->get_id());
    if (ptr_ != q->qordermap.end())
    {
      order_found = true;
    }
    ASSERT(order_found, "order not found");
    ASSERT(tim > 0, "no tim");
    q->fill_notify(s, s->get_sz(), -1, px, en::mt::EXECD, tim, &ret_path_);
    // if prev order is sim fill it for remainder
    if ((sz - fillsz > 0) && prev && static_cast<Order *>(prev)->issim())
      fill(q, static_cast<Order *>(prev), sz - fillsz, px, tim);
  }
  else
  {
    log_dbg("filling order partially o->sz: %d < sz: %d, fillsz: %d", s->get_sz(), sz, fillsz);
    bool order_found = false;
    auto ptr_ = q->qordermap.find(s->get_id());
    if (ptr_ != q->qordermap.end())
    {
      order_found = true;
    }

    ASSERT(order_found, "order not found");
    ASSERT(tim>0,"no tim");
    q->fill_notify(s, fillsz, -1, px, en::mt::EXEC, tim, &ret_path_);
    ASSERT(s->get_sz() > 0, "partial fill but size is < 0");
  }
}

void act::OB::notifybbbosubs(
    uint64_t txtim,
    en::bs side,
    en::x venue)
{
  // Throttle: minimum 100ms between notifications (max 10/sec)
  // txtim is in nanoseconds.
  //
  // Compile with -DBBBO_NO_THROTTLE to disable this throttle entirely,
  // e.g. for the arrival-process BBO tape recorder (bin_replay_bbo) which
  // needs every top-of-book change, not a decimated 10/sec view. Production
  // keeps the throttle on to bound SOM/light hop budgets.
#ifndef BBBO_NO_THROTTLE
  const uint64_t MIN_INTERVAL_NS = 100'000'000;  // 100ms in nanoseconds

  if (last_bbo_notify_tim != 0) {
    uint64_t elapsed = txtim - last_bbo_notify_tim;
    if (elapsed < MIN_INTERVAL_NS) {
      // Too soon, skip this notification
      return;
    }
  }
#endif

  if (best_bid >= best_ask)
  {
    // Locked (bid == ask) or inverted (bid > ask): do not publish.
    //
    // This used to test `==` only, so an INVERTED book was published despite
    // the comment saying otherwise. That gap was unreachable while a surviving
    // inversion aborted the run. It is reachable now that live uncrosses and
    // carries on: one pass can leave the book inverted, and the next real
    // record moves one side and notifies -- shipping BBBOChg(bid > ask) to
    // every subscriber. SOM caches it and Position::unrealPnl then marks longs
    // at an inflated bid AND shorts at a deflated ask, so unrealised PnL is
    // overstated on both sides at once, and the lights price placements off a
    // negative spread.
    return;
  }

  // Update timestamp before sending
  last_bbo_notify_tim = txtim;

  // Send BBBOChg to subscribers (locked market already filtered above at line 1079)
  for (const auto& c : bbbosubs)
  {
    // ASSERT(best_bid != best_ask, "locked bbbo");
    // log_dbg("sending bbbochg to %s", c->get_name());
    msg::BBBOChg *m = new msg::BBBOChg(
        txtim,
        venue,
        side,
        sym,
        int(best_bid),  // the cast is here because prices are ints outside of the ob but uints in ob
        int(best_ask)); // the assumption here is that prices are positive (this is not correct)
    // The inbound hop, like everything else this book tells a subscriber.
    // bbbosubs is the SAME list that receives TradeNotify through
    // publish_delayed, so sending this inline handed one subscriber the new BBO
    // instantly and the trade that caused it feed_delay later. SOM subscribes
    // here -- bbbochg_handler writes best_bid/best_ask/currtim straight from it
    // and those marks feed the unrealised PnL readouts -- so it was marking
    // positions against a book state nothing else had been allowed to see,
    // while the fills it was marking arrived delayed. txtim is this record's
    // own market time, which is what the rest of the publish path uses.
    publish_delayed(c, m, txtim);
  }
}

void act::OB::process_message(cmsgt msg) noexcept
{

  if (typeid(*msg) == typeid(frame::mtim::msg::Alarm))
  {
    SNGH;
  }
  else if (typeid(*msg) == typeid(msg::BBBOSub))
  {
    log_inf("got bbbosub from %s", msg->sender->get_name());
    bbbosubs.push_back(msg->sender);
  }
  else if (typeid(*msg) == typeid(mda::msg::Subscribe))
  {
    auto m = static_cast<const mda::msg::Subscribe *>(msg);
    ASSERT(m->sender, "no sender");
    if (m->prio == mda::msg::Subscribe::HI)
    {
      log_inf("hi prio sub from %s", m->sender->get_name());
      hiprio_datasubs.push_back(m->sender);
    }
    else if (m->prio == mda::msg::Subscribe::LOW)
    {
      log_inf("low prio sub from %s", m->sender->get_name());
      lowprio_datasubs.push_back(m->sender);
    }
    else
      SNGH;

    // Check for duplicate subscribers across all subscription lists
    std::set<actors::Actor*> all_unique_subs;

    // Check hiprio_datasubs for duplicates
    for (const auto& sub : hiprio_datasubs) {
      if (!all_unique_subs.insert(sub).second) {
        log_err("Duplicate subscriber found across subscription lists: %s", sub->get_name());
        ASSERT(false, "Subscriber exists in multiple subscription lists");
      }
    }

    // Check lowprio_datasubs for duplicates against the same set
    for (const auto& sub : lowprio_datasubs) {
      if (!all_unique_subs.insert(sub).second) {
        log_err("Duplicate subscriber found across subscription lists: %s", sub->get_name());
        ASSERT(false, "Subscriber exists in multiple subscription lists");
      }
    }
  }
}

void act::OB::data_handler(const frame::mda::msg::Data *m) noexcept
{

  // Change types to uint64_t
  uint64_t extim = 0, sendtim = 0;

  // Set when a CHANGE/DELETE carries a price we cannot put on the ladder: the
  // record is unusable but the order it refers to must still leave the book.
  bool badpx_force_delete = false;

  //data_handler_enter = chutil::Time::epoch();

  // auto a = frame::ref::RefData::inst().get_asset(sym);

  if (!m->israw)
  {
    log_trc("non raw");
  }

  if (std::holds_alternative<bfile::l3_som_t>(m->l3))
  {
    return;
  }

  ex_sym_id = 0;

  if (std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
  {
    const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);

    // Reject a price that cannot exist before it reaches the ladder.
    //
    // Observed 2025-01-30 on ESH5 (1 record in 11,673,971): an order resting at
    // 610600 was CHANGEd to 610608800, then back to 608800 nine seconds later.
    // That value is 610600 and 608800 run together — a packing/decode fault in
    // the source, not a market event. Converted it is 24,424,352 ticks against
    // a ladder of 25,592, so it indexes ~1000x past the end of askqs and the
    // process dies on a signal rather than an assert.
    //
    // The existing `px >= maxprice` guards in add()/mod() are too late: the
    // payload (and its Price conversion) is built here first. Drop the record
    // and carry on — a single corrupt print is not worth losing a session, but
    // it must be visible.
    {
      // Convert with ref::Price, NOT by hand. Price is what turns a price into
      // the tick index that addresses bidqs/askqs, so doing the arithmetic
      // separately here means the guard can disagree with the thing it exists
      // to guard -- which is exactly what happened: the guard divided by
      // Asset::units while the ladder was sized from minPriceIncrement, and
      // when the two conventions diverged by dispFactor the guard rejected
      // every record in the session without anything looking wrong.
      const int ticks = ref::Price((long double)mbo.pxd, sym).to_int();
      if (!(ticks > 0 && ticks < maxprice))
      {
        ++num_bad_px;
        // An ADD we cannot place is simply not in our book, and nothing later
        // will find it -- safe to drop.
        //
        // A CHANGE or DELETE is NOT safe to drop. The order is already resting
        // here, and discarding the message that moves or removes it leaves it
        // parked at a price the exchange has vacated: phantom liquidity that
        // never goes away and eventually crosses the book. That is exactly how
        // 2025-02-10 failed -- order 6414438546046 was repriced from 24347t to
        // 48622t, past the end of the ladder, the CHANGE was dropped, and the
        // stale ask sat under the bid until the invariant caught it 4 minutes
        // later.
        //
        // We cannot represent where the order went, so treat it as gone: fall
        // through to the delete path, which prices the cancel from the order's
        // last known good price in ordermap rather than from this record.
        if (mbo.orderUpdateAction == 0)
        {
          log_err("BAD PRICE %s: dropping ADD oid=%llu side=%c pxd=%.1f "
                  "(%d ticks, maxpx %d) tx=%llu",
                  get_name(), (unsigned long long)mbo.orderID,
                  mbo.side, mbo.pxd, ticks, maxprice,
                  (unsigned long long)mbo.transactTime);
          return;
        }
        log_err("BAD PRICE %s: oid=%llu action=%d moved off the ladder "
                "(pxd=%.1f, %d ticks, maxpx %d) tx=%llu -- removing it from the book",
                get_name(), (unsigned long long)mbo.orderID, int(mbo.orderUpdateAction),
                mbo.pxd, ticks, maxprice, (unsigned long long)mbo.transactTime);
        badpx_force_delete = true;
      }
    }

    current_mbo = mbo;
    ex_sym_id = get_ex_id(sym);
    if (mbo.securityID != ex_sym_id && mbo.securityID != int(sym))
    {
      //std::cerr << "OB " << get_name() << ": mbo.securityID=" << mbo.securityID << ", ex_sym_id=" << ex_sym_id << ", sym=" << sym << std::endl;
      log_trc("mbo.securityID: %d, expecting ex_sym_id: %d, or sym: %d",
              mbo.securityID, ex_sym_id, sym);
      return;
    }
    // Assign directly, do not call from_epoch
    extim = mbo.transactTime;
    sendtim = mbo.sendingTime;
    txtim_epoch = mbo.transactTime;
    sendtim_epoch = mbo.sendingTime;
    handlerendtim = mbo.handlerendtim;
    if (start_debug && txtim_epoch > start_debug)
      debug = true;
  }
  else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(m->l3))
  {
    // all books get all messages as trades do not have sec id
    const auto &mbot_ = std::get<bfile::l3_mbo_trd_v2_t>(m->l3);
    if (is_option)
      return;
    extim = mbot_.transactTime;
    sendtim = mbot_.sendingTime;
    txtim_epoch = mbot_.transactTime;
    sendtim_epoch = mbot_.sendingTime;
    handlerendtim = mbot_.handlerendtim;
    volumeall += mbot_.lastQty;
    if (start_debug && txtim_epoch > start_debug)
      debug = true;
  }
  else if (std::holds_alternative<bfile::l3_mbp_t>(m->l3))
  {
    #ifdef USEMBP
    const auto &mbp = std::get<bfile::l3_mbp_t>(m->l3);
    ex_sym_id = get_ex_id(sym);
    if (mbp.securityID != ex_sym_id || mbp.securityID != int(sym))
    {
      // all books get all messages as trades do not have sec id
      log_trc("mbp.securityID: %d, expecting ex_sym_id: %d, or sym: %d",
              mbp.securityID, ex_sym_id, sym);
      return;
    }

    ASSERT(mbp.securityID == ex_sym_id || mbp.securityID == int(sym), "bad sec");
    auto a = ref::RefData::get_asset_from_sec_id(mbp.securityID);
    ASSERT(a, "asset not found");

    if (mbp.pxlevel < 10)
    {
      if (mbp.side == '0')
      {
        mbp_bid_px[mbp.pxlevel - 1] = int(mbp.pxd / a->get_units() + .1);
        mbp_bid_sz[mbp.pxlevel - 1] = mbp.sz;

        if (is_option)
        {
          SNGH; // not used
          if (mbp.pxlevel == 1)
          {
            auto _best_bid = int(mbp.pxd / a->get_units() + .1);
            if (best_bid != _best_bid)
            {
              best_bid = _best_bid;
              notifybbbosubs(extim, mbp.transactTime, en::bs::BUY);
            }
          }
        }
      }
      else if (mbp.side == '1')
      {
        mbp_ask_px[mbp.pxlevel - 1] = int(mbp.pxd / a->get_units() + .1);
        mbp_ask_sz[mbp.pxlevel - 1] = mbp.sz;

        if (is_option)
        {
          SNGH; // not used
          if (mbp.pxlevel == 1)
          {
            auto _best_ask = int(mbp.pxd / a->get_units() + .1);
            if (best_ask != _best_ask)
            {
              best_ask = _best_ask;
              notifybbbosubs(extim, mbp.transactTime, en::bs::SEL);
            }
          }
        }
      }
      else if (mbp.side == 'E')
      {
        impl_bid_px[mbp.pxlevel - 1] = int(mbp.pxd / a->get_units() + .1);
        impl_bid_sz[mbp.pxlevel - 1] = mbp.sz;
      }
      else if (mbp.side == 'F')
      {
        impl_ask_px[mbp.pxlevel - 1] = int(mbp.pxd / a->get_units() + .1);
        impl_ask_sz[mbp.pxlevel - 1] = mbp.sz;
      }
      else
        SNGH;
    }
    #else
    return;
    #endif
  }
  else if (std::holds_alternative<bfile::l3_mbp_trd_t>(m->l3))
  {
    #ifdef USEMBP
    const auto &mbpt = std::get<bfile::l3_mbp_trd_t>(m->l3);
    ex_sym_id = get_ex_id(sym);
    if (mbpt.securityID != ex_sym_id || mbpt.securityID != int(sym))
    {
      // all books get all messages as trades do not have sec id
      log_trc("mbpt.securityID: %d, expecting ex_sym_id: %d, or sym: %d",
              mbpt.securityID, ex_sym_id, sym);
      return;
    }

    ASSERT(mbpt.securityID == ex_sym_id || mbpt.securityID == int(sym), "bad sec");
    volumembp += mbpt.sz;
    return;
    #else
    return;
    #endif
  }
  else if (std::holds_alternative<bfile::l3_vol_t>(m->l3))
  {
    const auto &vol = std::get<bfile::l3_vol_t>(m->l3);
    ex_sym_id = get_ex_id(sym);
    if (vol.securityID != ex_sym_id || vol.securityID != int(sym))
    {
      // all books get all messages as trades do not have sec id
      log_trc("vol.securityID: %d, expecting ex_sym_id: %d, or sym: %d",
              vol.securityID, ex_sym_id, sym);
      return;
    }

    ASSERT(vol.securityID == ex_sym_id || vol.securityID == int(sym), "bad sec");
    log_inf("time: %d, cme vol: %d, typ: %d, mbo vol found: %d, mbo vol all: %d, mbombp vol: %d",
            vol.txtim, vol.vol, int(vol.vtyp),
            volumefound, volumeall, volumembp);
    return;
  }
  else if (std::holds_alternative<bfile::l3_sst_t>(m->l3))
  {
    // Security status. These carry no securityID -- CME leaves it null
    // (INT32_MAX) because the message applies to the whole group -- so BFA
    // broadcasts them to every book and each one tracks its own copy.
    const auto &sst = std::get<bfile::l3_sst_t>(m->l3);
    const bool was = matching;
    matching = is_matching_status(sst.tradingStatus);
    if (was != matching)
      log_opr("%s trading status %u (haltReason %u): matching %s",
              get_name(), unsigned(sst.tradingStatus), unsigned(sst.haltReason),
              matching ? "ON" : "OFF");
    return;
  }
  else if (
      std::holds_alternative<bfile::l3_chr_v2_t>(m->l3) ||
      std::holds_alternative<bfile::l3_eob_t>(m->l3) ||
      std::holds_alternative<bfile::l3_gap_v2_t>(m->l3) ||
      std::holds_alternative<bfile::l3_sim_t>(m->l3))
  {
  }
  else
  {
    ASSERT(m->israw, "cannot ignore sim");
    return;
  }

  if (std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
  {

    const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);

    if (is_option)
      return;

#ifdef CHECKLATENCY
    auto timeutc = chutil::Time::now_utc();

    if (m->l3.mbo.sendingTime > 0 && !m->l3.mbo.recovery)
      latency.push_back(timeutc - sendtim);

// if (latency.size() > 10000)
// {
//   oogsl::gvector gv(latency);
//   gv.sort();
//   auto q01 = gv.quantile(.01);
//   auto q05 = gv.quantile(.05);
//   auto q25 = gv.quantile(.25);
//   auto q50 = gv.quantile(.5);
//   auto q75 = gv.quantile(.75);
//   auto q85 = gv.quantile(.85);
//   auto q95 = gv.quantile(.95);
//   auto q99 = gv.quantile(.99);
//   log_inf("OB Latency n: %d, q01: %f, q05: %f, q25: %f, q50: %f, q75: %f, q85: %f, q95: %f, q99: %f",
//           latency.size(), q01, q05, q25, q50, q75, q85, q95, q99);
//   latency.clear();
// }
#endif

    ASSERT(m->israw, "must be raw");

    if (extim != 0)
    {
      if (mbo.recovery)
      {
        last_recovery_tx_tim = extim;
      }
      else
      {
        last_tx_tim = extim;
      }
    }

    if (extim != 0 && last_tx_tim != 0 && last_recovery_tx_tim != 0)
    {
      if (mbo.recovery)
      {
        if (extim < last_tx_tim)
        {
          log_inf("rejecting recovery message extim: %lu, last_tx_tim: %lu",
                  extim, last_tx_tim);
          return;
        }
        last_recovery_tx_tim = extim;
      }
      else
      {
        if (extim <= last_recovery_tx_tim)
        {
          log_dbg("rejecting data message extim: %lu, last_recovery_tx_tim: %lu",
                  extim, last_recovery_tx_tim);
          return;
        }
        last_tx_tim = extim;
      }
    }

    // initiate clear
    if (mbo.recovery && !is_in_recovery)
    {
      log_inf("got a recovery message initiating clear");
      clear();
      is_in_recovery = true;
    }

    // end of recovery
    if (!mbo.recovery && is_in_recovery)
    {
      log_inf("end of recovery");
      is_in_recovery = false;
    }
  }

  if (!m->israw)
  {
    // this is a sim order from SOM
    ASSERT(m->payload->is_sim(), "not sim");
    // put this on the q
    ASSERT(currtim > 0, "cannot set send time in sim order"); // why? because del q will not work?
    auto ncd = const_cast<frame::mda::msg::Data *>(m);
    auto pl = const_cast<frame::mda::msg::data_pay_load *>(ncd->payload.get());
    pl->send_tim = currtim;
    pl->txtim_epoch = currtim;
#ifdef TRACEORDERS
    cerr << "Del Q push order " << mda::OrderID::id(pl->order_ref) << " " << currtim.to_string() << endl;
#endif
    // ts0 == 0 means the sender had no market time to give: a console cancel,
    // or one of the SOM's own unwind cancels on shutdown. Apply it now rather
    // than invent a timestamp to delay it by. Those messages are not part of
    // the measured experiment, and a made-up ts0 would either release the
    // message instantly anyway (if it were in the past) or strand it on the
    // queue forever (if it were in the future) -- both worse than being
    // honest that there is nothing to model here.
    if (m->payload->ts0 == 0)
    {
      // Who actually sends one of these? The reasoning above says "console or
      // shutdown unwind, therefore outside the experiment" -- but that has
      // never been measured, and if a cancel on the NORMAL path ever arrives
      // here it is a silent correctness bug, not a harmless shortcut: the
      // cancel jumps the queue and can be applied BEFORE the order it cancels
      // has been released from del_q. mod() then finds nothing in qordermap,
      // replies CancReject(NOTFOUND), and the order pops out afterwards and
      // rests in the book forever -- invisible to SOM, and filled for real
      // when the market trades through it.
      //
      // So assert, and let a run tell us. If this fires, read the order ref in
      // the message: an id SOM knows is the bug; a console/shutdown cancel is
      // the documented case and the assert should become a whitelist of those
      // two senders rather than being removed.
      ASSERTF(del_q.empty(),
              boost::format("%s untimed cancel (ts0 == 0) while %zu sim orders are "
                            "still queued: it would jump the queue and could be "
                            "applied before the order it cancels. order id %d")
                % get_name() % del_q.size() % mda::OrderID::id(pl->order_ref));
      log_opr("%s sim order with no ts0, applying without delay id: %d",
              get_name(), mda::OrderID::id(pl->order_ref));
      process_market_data(m->payload, m->sender);
      return;
    }

    auto ts0_tim = chutil::Time::from_epoch(m->payload->ts0);
    ASSERT(ts0_tim.is_valid(), "bad ts0");
    del_q.push_back(make_tuple(m->payload, m->sender));
    log_dbg("pushing sim order on the queue id: %d", mda::OrderID::id(pl->order_ref));
    return;
  }

  if (
      std::holds_alternative<bfile::l3_chr_v2_t>(m->l3) ||
      std::holds_alternative<bfile::l3_eob_t>(m->l3) ||
      std::holds_alternative<bfile::l3_gap_v2_t>(m->l3))
  {
    //
  }

  auto is_add = [](const bfile::l3_t &l3)
  {
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
    {
      const auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
      return mbo.orderUpdateAction == 0;
    }
    return false;
  };

  auto is_canc = [](const bfile::l3_t &l3)
  {
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
    {
      const auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
      return mbo.orderUpdateAction == 1;
    }
    return false;
  };

  auto is_cand = [](const bfile::l3_t &l3)
  {
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
    {
      const auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
      return mbo.orderUpdateAction == 2;
    }
    return false;
  };

  auto is_exec = [](const bfile::l3_t &l3)
  {
    if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3))
    {
      return true;
    }
    return false;
  };

  auto get_side = [](const bfile::l3_t &l3)
  {
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
    {
      const auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
      if (mbo.side == '0')
        return en::bs::BUY;
      else if (mbo.side == '1')
        return en::bs::SEL;
      else
        SNGH;
    }
    ERR("must be mbo");
    return en::bs::UNK;
  };

  auto have_side = [](const bfile::l3_t &l3)
  {
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
    {
      const auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
      return mbo.side == '0' || mbo.side == '1';
    }
    return false;
  };

  if (std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
  {
    const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);
    if (mbo.recovery)
      ASSERT(is_add(m->l3), "must have add for recovery");
  }

  /**
   * @brief process slated exec
   * @param ordref id of order to be cancelled
   * @param pl payload for the canc or cancd
   *
   */
  auto process_slated_exec = [this](auto ordref, auto pl)
  {
    auto p = exec_slate.find(ordref);
    if (p == exec_slate.end())
    {
      process_q(pl.get());
    }
    else
    {
      auto exec_ptr = p->second.front();
      p->second.pop();
      if (p->second.empty())
      {
        exec_slate.erase(p);
      }
      process_q(exec_ptr.get());
      process_q(pl.get());
    }
  };

  if (is_add(m->l3) && !badpx_force_delete)
  {

    num_add++;

    if (!std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
    {
      ERR("not mbo");
      return;
    }

    const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);
    {
      // check for order added twice

      bool found = false;
      auto ptr_ = ordermap.find(mbo.orderID);
      if (ptr_ != ordermap.end())
      {
        found = true;
      }

      if (found)
      {
        log_err("order added twice: %d", mbo.orderID);
        OBFILE << "OBX: order added twice: " << mbo.orderID << endl;
        OBFILE.flush();
        // NOTE:
        // it may be a good idea to clear the book here
        // since if order was added twice then most likely
        // recovery was initiated twice in a row with
        // no packets arriving inbetween
        return;
      }
    }

    if (mbo.pxd <= 0)
    {
      log_err("bad px: %d", mbo.pxd);
      // ERR("bad px");
      return;
    }

    order_info_t ord; 
    auto ordref = mda::OrderID::longid(mbo.venue, bookoid++);
    ord.chopid = ordref;
    ord.xoid = mbo.orderID;
    ord.px = mbo.pxd;
    ord.sz = mbo.displayQty;
    ord.side = get_side(m->l3);
    ordermap[ord.xoid] = ord;

    ASSERT(ord.sz > 0, "sz");

    auto pl = mda::msg::data_pay_load::make_payload(
        mbo.handlerendtim,
        get_side(m->l3),
        mbo.orderID,
        ordref,
        sym,
        en::md::ADD,
        en::mt::NONE,
        mbo.displayQty,
        mbo.displayQty,
        mbo.pxd,
        extim,
        sendtim,
        mbo.venue,
        mbo.endOfEvent,   // NOT || lastQuote: that is set on 99.6% of records
        mbo.recovery);
    pl->txtim_epoch = txtim_epoch;
    pl->sendtim_epoch = sendtim_epoch;
    pl->hndl_tim_epoch = handlerendtim;

    process_q(pl.get());
  }
  else if (is_exec(m->l3))
  {

    //
    // note:
    // execs have no impact on the book
    // used only for simulation and accounting
    //

    if (!std::holds_alternative<bfile::l3_mbo_trd_v2_t>(m->l3))
    {
      ERR("not mbt");
      return;
    }

    const auto &mbot = std::get<bfile::l3_mbo_trd_v2_t>(m->l3);

    bool found = false;
    auto ptr_ = ordermap.find(mbot.orderID);
    order_info_t *ord = nullptr;
    if (ptr_ != ordermap.end())
    {
      found = true;
      ord = &ptr_->second;
    }

    if (!found)
      return;

    num_exec++;
    //log_dbg("num_exec: %d, num_add: %d", num_exec, num_add);

    volumefound += mbot.lastQty;

    auto ordref = ord->chopid;

    auto pl = mda::msg::data_pay_load::make_payload(
        mbot.handlerendtim,
        ord->side,
        mbot.orderID,
        ordref,
        sym,
        en::md::MOD,
        en::mt::EXEC,
        mbot.lastQty,
        0,
        ord->px,
        extim,
        sendtim,
        mbot.venue,
        mbot.endOfEvent,  // NOT || lastTrade: per-order flag, set on 96% of trades
        false);
    pl->txtim_epoch = txtim_epoch;
    pl->sendtim_epoch = sendtim_epoch;
    pl->hndl_tim_epoch = handlerendtim;

    //
    // not sure if a q is necessary or is the
    // correspondence of execs to cancs 1 to 1 ?
    //
    exec_slate[ordref].push(pl);
  }
  else if (is_canc(m->l3) && !badpx_force_delete)
  {

    if (!std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
    {
      ERR("not mbo");
      return;
    }

    const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);

    // if (mbo.orderID == 401903335461)
    // {
    //   log_inf("canc: %d", mbo.orderID);
    // }

    bool found = false;
    order_info_t *ord = nullptr;
    auto ptr_ = ordermap.find(mbo.orderID);
    if (ptr_ != ordermap.end())
    {
      found = true;
      ord = &ptr_->second;
    }

    if (!found)
    {
      // log_wrn("order not found on canc: %d", m->l3.mbo.orderID);
      return;
    }

    auto ordref = ord->chopid;

    auto eq = [](double a, double b)
    {
      auto epsilon = std::numeric_limits<double>::epsilon();
      return fabs(a - b) <= ((fabs(a) < fabs(b) ? fabs(b) : fabs(a)) * epsilon);
    };

    if (!have_side(m->l3))
    {
      // check if anything changed to make this a canc replace
      // the assert is that in ITCH canc replace is not allowed
      // if we dont have side we dont have price
      // ASSERT(eq(mbo.pxd, (*ord)->px), "px");
      ASSERT(mbo.displayQty <= ord->sz, "sz");
    }

    //
    // note that in ITCH a canc may have a differnt sz but there is no side or price
    //

    if (mbo.displayQty > ord->sz || (have_side(m->l3) && !eq(mbo.pxd, ord->px)))
    {

      // cancel replace

      // delete
      auto pl1 = mda::msg::data_pay_load::make_payload(
          mbo.handlerendtim,
          ord->side,
          mbo.orderID,
          ordref,
          sym,
          en::md::MOD,
          en::mt::CANCD,
          0,
          0,
          ord->px,
          extim,
          sendtim,
          mbo.venue,
          false,
          false);
      pl1->txtim_epoch = txtim_epoch;
      pl1->sendtim_epoch = sendtim_epoch;
      pl1->hndl_tim_epoch = handlerendtim;

      process_slated_exec(ordref, pl1);

      // now add
      ordref = mda::OrderID::longid(mbo.venue, bookoid++);
      auto pl2 = mda::msg::data_pay_load::make_payload(
          mbo.handlerendtim,
          have_side(m->l3) ? get_side(m->l3) : ord->side,
          mbo.orderID,
          ordref,
          sym,
          en::md::ADD,
          en::mt::NONE,
          mbo.displayQty,
          mbo.displayQty,
          have_side(m->l3) ? mbo.pxd : ord->px,
          extim,
          sendtim,
          mbo.venue,
          mbo.endOfEvent,   // NOT || lastQuote: that is set on 99.6% of records
          false);
      pl2->txtim_epoch = txtim_epoch;
      pl2->sendtim_epoch = sendtim_epoch;
      pl2->hndl_tim_epoch = handlerendtim;

      process_q(pl2.get());

      // todo: what happens with ord2
      auto ord2 = const_cast<order_info_t *>(ord);
      (ord2)->chopid = ordref;
      (ord2)->xoid = mbo.orderID;
      (ord2)->sz = mbo.displayQty;
      if (have_side(m->l3))
      {
        (ord2)->px = mbo.pxd;
        ASSERT((ord2)->side == get_side(m->l3), "side has changed");
      }
    }
    else
    {
      // a regular canc
      auto ordref = (ord)->chopid;
      (ord)->sz = mbo.displayQty;
      auto pl = mda::msg::data_pay_load::make_payload(
          mbo.handlerendtim,
          (ord)->side,
          mbo.orderID,
          ordref,
          sym,
          en::md::MOD,
          en::mt::CANC,
          0,
          mbo.displayQty,
          (ord)->px,
          extim,
          sendtim,
          mbo.venue,
          mbo.endOfEvent,   // NOT || lastQuote: that is set on 99.6% of records
          mbo.recovery);
      pl->txtim_epoch = txtim_epoch;
      pl->sendtim_epoch = sendtim_epoch;
      pl->hndl_tim_epoch = handlerendtim;

      process_slated_exec(ordref, pl);
    }
  }
  else if (is_cand(m->l3) || badpx_force_delete)
  {

    if (!std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
    {
      ERR("not mbo");
      return;
    }

    const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);

    bool found = false;
    order_info_t *ord = nullptr;
    auto ptr_ = ordermap.find(mbo.orderID);
    if (ptr_ != ordermap.end())
    {
      found = true;
      ord = &ptr_->second;
    }

    // cancd


    if (!found)
      return;

    auto ordref = (ord)->chopid;

    auto pl = mda::msg::data_pay_load::make_payload(
        mbo.handlerendtim,
        (ord)->side,
        mbo.orderID,
        ordref,
        sym,
        en::md::MOD,
        en::mt::CANCD,
        0,
        0,
        (ord)->px,
        extim,
        sendtim,
        mbo.venue,
        mbo.endOfEvent,   // NOT || lastQuote: that is set on 99.6% of records
        mbo.recovery);
    pl->txtim_epoch = txtim_epoch;
    pl->sendtim_epoch = sendtim_epoch;
    pl->hndl_tim_epoch = handlerendtim;

    process_slated_exec(ordref, pl);

    ordermap.erase(mbo.orderID);
  }
  else if (
      std::holds_alternative<bfile::l3_chr_v2_t>(m->l3) ||
      std::holds_alternative<bfile::l3_gap_v2_t>(m->l3) ||
      std::holds_alternative<bfile::l3_eob_t>(m->l3))
  {
    //auto &sym_s = ref::RefData::inst().get_asset_name(sym);
    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(new mda::msg::data_pay_load());
    payload->ts0 = 0;
    payload->sym = sym;
    //strcpy(payload->sym_str, sym_s.c_str());
    if (std::holds_alternative<bfile::l3_chr_v2_t>(m->l3))
    {
      const auto &chr = std::get<bfile::l3_chr_v2_t>(m->l3);
      payload->mev = en::md::CLEAR;
      payload->mkt = chr.venue;
      // strcpy(payload->mkt_str, en::to_string(chr.venue));
    }
    else if (std::holds_alternative<bfile::l3_gap_v2_t>(m->l3))
    {
      const auto &gap = std::get<bfile::l3_gap_v2_t>(m->l3);
      payload->mev = en::md::GAP;
      payload->mkt = gap.venue;
      // strcpy(payload->mkt_str, en::to_string(gap.venue));
    }
    else if (std::holds_alternative<bfile::l3_eob_t>(m->l3))
    {
      // const auto& eob=std::get<bfile::l3_eob_t>(m->l3);
      payload->mev = en::md::EOBURST;
      payload->mkt = en::x::UNI;
      // strcpy(payload->mkt_str, en::to_string(eob.venue));
      if (have_new_payload && last_good_payload->eoe)
      {
        cross_check(last_good_payload);
      }
    }
    else
      SNGH;

    payload->txtim_epoch = txtim_epoch;
    payload->sendtim_epoch = sendtim_epoch;
    payload->hndl_tim_epoch = handlerendtim;
    process_market_data(payload, 0);
  }
  else if (std::holds_alternative<bfile::l3_mbp_t>(m->l3))
  {
    // do nothing
  }
  else if (std::holds_alternative<bfile::l3_mbp_trd_t>(m->l3))
  {
    // do nothing
  }
  // else if (std::holds_alternative<bfile::l3_vol_t>(m->l3))
  // {
  //   // do nothing
  // }
  // else if (std::holds_alternative<bfile::l3_sim_t>(m->l3))
  // {
  //   // do nothing
  // }
  else
  {
    SNGH;
  }

}

// #define DBGDELQ

void act::OB::process_q(const mda::msg::data_pay_load *to_proc)
{
  // ASSERT(to_proc->px.to_int() > 0, "zero price");

  if (to_proc->px.to_int() <= 0)
  {
    log_err("negative prices not implemented");
#ifdef TRACEORDERS
    cerr << "<=0 prices not implemented" << endl;
#endif
    return;
  }

#ifdef TRACEORDERS
if (debug)
  cerr << "process_q: " << mda::OrderID::id(to_proc->order_ref) << " " << to_proc->send_tim.to_string() << endl;
#endif

  ASSERT(!to_proc->is_sim(), "this must be a real order");

  // Our own orders on del_q are released BELOW, before this record is applied,
  // and the fills and cancel-acks they generate are exchange events stamped
  // with `last_processed_ts`. Advance it first. Without this the ack carries
  // the PREVIOUS record's time -- one inter-record gap too early, and on a
  // quiet book that gap is far larger than the feed delay it is supposed to
  // pay, so the ack comes back before it was even generated.
  if (to_proc->txtim_epoch)
    last_processed_ts = to_proc->txtim_epoch;

  while (true)
  {
    auto p = del_q.begin();
    if (p == del_q.end())
      break;

    log_dbg("have orders on q");

    auto p2 = p;
    ++p2;
    auto p_ = get<0>(*p);
    ASSERT(p_->send_tim > 0, "bad date");

    // the normal scenario:
    // ts0 is end of handler time
    // compare sim send time to to_proc (real order)
    // transaction time
    ASSERT(p_->ts0 > 0, "bad ts0");
    auto ts0_tim = p_->ts0;
    auto order_leave_time = ts0_tim; // p_->ts0 > 0 ? ts0_tim : p_->send_tim;
#ifdef OB_TAIL_DELAY
    // Lindley recursion on the outbound queue (paper §4.1 eqs (3)-(4), §5.2):
    //     d_i = max(a_i, d_{i-1}) + s_out.
    // Cancels and sends share the queue and the same service time under this
    // build; the cancel_delay / feed_delay / 40us-floor composition of the
    // constant-lag path is bypassed. Arrival is ts0 (the tape time SOM decided
    // to act), not a constant-delay-shifted stamp. State is advanced only when
    // we actually release below, so recomputation on later ticks is idempotent.
    ASSERTF(service_us_outbound > 0,
            boost::format("OB_TAIL_DELAY: service_us_outbound=%d must be set "
                          "to a positive value before orders are processed "
                          "(ob_set_service_us_outbound)")
              % service_us_outbound);
    const uint64_t order_engine_arrive_time =
        std::max<uint64_t>(order_leave_time, last_release_outbound_ns)
          + uint64_t(service_us_outbound) * 1000;
    [[maybe_unused]] const int eff_delay = service_us_outbound; // logging only, below
#else
    // A cancel and a new order traverse the same wire, so cancel_delay is
    // normally -1 (= delay); it exists only so an experiment can make the two
    // asymmetric. Note ts0 for a cancel is the time the CANCEL was decided,
    // not the original order's -- see SOM::cancel_order.
    const int eff_delay = (p_->action == en::mt::CANCD && cancel_delay >= 0)
                            ? cancel_delay : delay;
    // uint64_t before the multiply: std::max(40, eff_delay) is an int, and an
    // int * 1000 overflows above ~2.1e6 us (~2.1 s). --ob-delay-us is an
    // unbounded po::value<int>, so a latency arm of 3 s used to wrap negative
    // and either release instantly or push the head of the queue so far into
    // the future that it never became ready -- wedging del_q for the rest of
    // the run, since the loop breaks on the first entry that is not ready.
    // ts0 is the market time the light SAW, which is already feed_delay old --
    // so the round trip is feed + order, not order alone. Leaving this at
    // ts0 + delay makes every order arrive one feed hop too early, which is the
    // asymmetry this whole change exists to remove. feed_delay 0 leaves the
    // arithmetic exactly as it was.
    auto order_engine_arrive_time =
        order_leave_time
          + uint64_t(feed_delay) * 1000
          + uint64_t(std::max(40, eff_delay)) * 1000; // 40 us floor
#endif
    if (order_engine_arrive_time < to_proc->tim)
    {

#if defined(TRACEORDERS) || defined(DBGDELQ)
      cerr << "POP order "
           << mda::OrderID::id(p_->order_ref)
           << " sent at " << order_leave_time.to_string()
           << " arrved at " << order_engine_arrive_time.to_string()
           << " because tx nowis " << to_proc->tim.to_string()
           << " d = " << eff_delay << endl;
#endif

      log_dbg("popping order id: %d sent at: %d, arrived at: %d, because time now is: %d, del: %d",
              mda::OrderID::id(p_->order_ref),
              order_leave_time,
              order_engine_arrive_time,
              to_proc->tim,
              eff_delay);

      // we let this order through
#ifdef OB_TAIL_DELAY
      // Advance the outbound Lindley state d_{i-1} := d_i only on release.
      // Recomputation on prior ticks that did NOT release must be idempotent.
      last_release_outbound_ns = order_engine_arrive_time;
#endif
      process_market_data(p_.get(), get<1>(*p));
      del_q.erase(p);
    }
    else
    {

#if defined(TRACEORDERS) || defined(DBGDELQ)
      cerr << "NO POP order " << mda::OrderID::id(p_->order_ref)
           << " sent at " << p_->send_tim.to_string()
           << " because trans time is "
           << to_proc->tim.to_string()
           << " delay = " << eff_delay
           << " current sent time is "
           << to_proc->send_tim.to_string()
           << endl;
#endif

      log_trc("NO POP order id: %d, sent at: %s, bacause tx time is: %s, del: %d, current time: %s",
              mda::OrderID::id(p_->order_ref), p_->send_tim.to_string(), to_proc->tim.to_string(), eff_delay, to_proc->send_tim.to_string());

      // leave the order on the q
      break;
    }
    p = p2;
  }
  process_market_data(to_proc, 0);
}

void act::OB::process_market_data(
    boost::intrusive_ptr<const mda::msg::data_pay_load> got_payload,
    actors::Actor *sender)
{

  ASSERT(got_payload->mev != en::md::UNI, "uninitialied md type");

  // Release any market data whose feed delay has elapsed, BEFORE this event
  // touches the book -- a subscriber must never be handed an update newer than
  // the one it is still waiting for. No-op when feed_delay is 0.
  //
  // Market data only. One of OUR orders coming off del_q also lands here, and
  // it carries the txtim of the record that was current when SOM sent it --
  // older than the record that has just released it. Letting that through
  // walked `last_processed_ts` BACKWARDS, so the fill or cancel-ack the order
  // generates was stamped in the past and came back before the feed hop it was
  // supposed to pay.
  if (got_payload->txtim_epoch && !got_payload->is_sim())
  {
    drain_pub_q(got_payload->txtim_epoch);
    last_processed_ts = got_payload->txtim_epoch;
  }

  // No-cross invariant, checked once a whole TRANSACTION has been applied —
  // i.e. on the first record of the next one.
  //
  // Neither per-record flag works here:
  //
  //  * endOfEvent marks the end of a PACKET, not of a transaction, and CME can
  //    put two flagged records in one transaction. 2025-01-10 tx
  //    1736515801223219749: the aggressing NEW bid at 23708 carries
  //    endOfEvent=1, and the DELETE of the ask at 23707 it just traded against
  //    arrives after it in the SAME transaction, also flagged. Checking on the
  //    first flag sees 23708 > 23707 — a book that is merely mid-transaction.
  //
  //  * lastQuote / lastTrade are per-order flags (set on 99.6% / 96% of
  //    records) and were previously OR-ed into eoe, making it fire constantly.
  //
  // A transaction is every record sharing one transactTime, and the book is
  // whole once all of them are applied. Both observed sweeps resolve inside a
  // single transactTime: 2025-02-10 tx 1739147206116582643 (NEW bid + 10 ask
  // deletes) and the 2025-01-10 case above.
  //
  // Locked (bid == ask) is permitted; only a genuine inversion aborts.
  //
  // And only while the instrument is MATCHING. When it is not -- pre-open, a
  // halt, or a Velocity Logic reserve -- CME accepts orders and cancels but
  // does not cross them off against each other, so a crossed book is the
  // exchange's own correct state and not our bug. Observed 2025-01-15 at the
  // CPI release: tradingStatus 21 (PreOpen) haltReason 2 (MarketEvent) at
  // 13:30:01.217639991, the exact nanosecond an aggressive bid rested 18 ticks
  // through the ask stack, then 15 -> 17 (ReadyToTrade) five seconds later at
  // 13:30:06.217000000, the exact nanosecond those orders were cleared. The
  // capture is complete across that window: no sequence gaps, and every order
  // involved has exactly its NEW and its DELETE.
  //
  // A boundary is transactTime ADVANCING, not merely differing. Real records
  // are not always monotonic -- 2025-01-15 tx 1736961632175701683 is followed
  // by a genuine exchange record 335ms EARLIER -- and on `!=` that reads as
  // "the transaction finished", so the invariant runs against a book that is
  // still mid-update and reports a cross that is not one.
  //
  // Our own orders are skipped outright: they arrive off del_q, they are not
  // exchange transactions, and they must neither define a boundary nor trip
  // the check.
  if (matching && xcheck_tx && !got_payload->is_sim() &&
      got_payload->tim > xcheck_tx)
  {
    // Say WHICH orders are crossed, not merely that the book is. Reproducing
    // this by replaying with tracing on is not practical: --ob-debug logs from
    // the first record, so reaching an afternoon cross means gigabytes of log
    // and a run an order of magnitude slower than the session itself. The
    // abort is the one moment the state is guaranteed interesting, so dump it
    // here -- every order resting between the two sides, which is exactly the
    // set that cannot legitimately coexist.
    if (best_bid > best_ask)
    {
      const auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
      OBFILE << "OBX CROSS DUMP " << get_name()
             << " tx=" << xcheck_tx << " bid=" << best_bid << " ask=" << best_ask << "\n";
      for (int px = best_ask; px <= best_bid && px < maxprice; px++)
      {
        for (int side = 0; side < 2; side++)
        {
          const OrderQ *q = side ? bidqs[px] : askqs[px];
          if (!q || q->isempty()) continue;
          OBFILE << (side ? "  BID " : "  ASK ") << px
                 << " sz=" << q->get_orders_in_book()
                 << " sim=" << q->get_sim_in_book() << " :";
          for (const frame::ob::OrderQNode *n = q->get_head(); n; n = n->next)
          {
            auto *o = static_cast<const frame::ob::Order *>(n);
            OBFILE << " " << mda::OrderID::id(o->get_id())
                   << ":ex" << o->get_exordid()
                   << ":sz" << o->get_sz()
                   << (o->issim() ? ":SIM" : "");
          }
          OBFILE << "\n";
        }
      }
      OBFILE.flush();
    }
    // FATAL IN SIMULATION, REPORTED IN LIVE.
    //
    // A crossed book means the reconstruction disagrees with the exchange, and
    // in a replay that must stop the run: every fill after it is fiction, and
    // a corpus quietly built on a broken book is worse than no corpus.
    //
    // Live cannot afford the same answer. NOASSERT is defined nowhere in the
    // build, so ASSERT is live in -O3 and this is abort() -- with real orders
    // resting at CME and no cancel-on-disconnect having run yet. The uncross
    // this PR adds runs only inside add(), so a cross introduced by mod() or
    // del() -- the mod_order_that_is_not_found path that returns early and
    // skips the BBO re-walk, the very case cited as motivation -- survives to
    // the next transaction and lands here. do_cross_check is exactly the flag
    // that says which of the two we are, and it was missing from this test.
    if (do_cross_check)
    {
      ASSERTF(best_bid <= best_ask,
              boost::format("CROSSED book %s after transaction %llu: best_bid %d > best_ask %d "
                            "(next record: %s px=%d sz=%d exordid=%llu tim=%llu)")
                % get_name() % xcheck_tx % best_bid % best_ask
                % en::to_string(got_payload->side) % got_payload->px.to_int()
                % got_payload->sz % got_payload->ex_order_id % got_payload->tim);
    }
    else if (best_bid > best_ask)
    {
      // Live: shout, count it, and keep the session alive. num_cross_recover is
      // the number that says how often the add()-path repair was not enough.
      ++num_cross_recover;
      log_err("CROSSED book %s after transaction %llu: best_bid %d > best_ask %d "
              "-- uncross did not repair it (n=%d)",
              get_name(), (unsigned long long)xcheck_tx, best_bid, best_ask,
              num_cross_recover);
    }
  }
  if (!got_payload->is_sim() && got_payload->tim > xcheck_tx)
    xcheck_tx = got_payload->tim;

  process_add_or_mod(got_payload, sender);



  if (got_payload->is_sim())
  {
    log_trc("got sim order not continuing");
    return;
  }

  if (got_payload->recovery)
    return;

  if (
      got_payload->mev == en::md::EOBURST ||
      got_payload->mev == en::md::TEST ||
      got_payload->mev == en::md::CLEAR ||
      got_payload->mev == en::md::GAP)
    return;

  currtim = got_payload->send_tim;
  ASSERT(currtim > 0, "invalid time");
  txtim_epoch = got_payload->txtim_epoch;

  auto a = ref::RefData::get_asset(got_payload->sym);
  ASSERT(a, "no asset");
  auto incr = a->bo_spread;
  ASSERT(incr == 1, "incr");
  ASSERT(maxprice >= a->maxpx, "maxprice");

  //
  // TODO: change these places to arrays of double
  //

  for (std::size_t i = 0; i < NLEVELS; i++)
  {
#ifdef USEPOINTPLACES
    bid_px[i].clear();
    ask_px[i].clear();
    bid_sz[i].clear();
    ask_sz[i].clear();
    cbid_sz[i].clear();
    cask_sz[i].clear();
    ba[i].clear();
#endif
  }

  auto calc_ba = [incr, this, a]()
  {
    int bsum = 0, asum = 0;
    auto mxpx = best_ask > a->maxpx - incr * NLEVELS * 2;
    auto lowpx = best_bid < incr * NLEVELS * 2;
    auto bid_px_ = &bid_px[0];
    auto ask_px_ = &ask_px[0];
    auto cbid_sz_ = &cbid_sz[0];
    auto cask_sz_ = &cask_sz[0];
    auto bid_sz_ = &bid_sz[0];
    auto ask_sz_ = &ask_sz[0];
    auto _bid_px = best_bid;
    auto _ask_px = best_ask;
    auto bid_q_ = &bidqs[_bid_px];
    auto ask_q_ = &askqs[_ask_px];
    for (std::size_t i = 0; i < bid_px.size(); i++)
    {
      // must have orders on both sides otherwise book is not calculated
      if (lowpx)
      {
        bid_px[i] = 0;
        ask_px[i] = a->maxpx - 1;
        cbid_sz[i] = 0;
        cask_sz[i] = 0;
        bid_sz[i] = 0;
        ask_sz[i] = 0;
        ba[i] = 1;
        continue;
      }
      if (mxpx)
      {
        bid_px[i] = 0;
        ask_px[i] = a->maxpx - 1;
        cbid_sz[i] = 0;
        cask_sz[i] = 0;
        bid_sz[i] = 0;
        ask_sz[i] = 0;
        ba[i] = 1;
        continue;
      }

      auto _bid_sz = (*bid_q_)->get_orders_in_book();
      auto _ask_sz = (*ask_q_)->get_orders_in_book();
      bid_q_ -= incr;
      ask_q_ += incr;

      bsum += _bid_sz;
      asum += _ask_sz;

      *bid_sz_++ = _bid_sz;
      *ask_sz_++ = _ask_sz;
      *bid_px_++ = _bid_px;
      *ask_px_++ = _ask_px;
      *cbid_sz_++ = bsum;
      *cask_sz_++ = asum;

      _bid_px -= incr;
      _ask_px += incr;

#define CHECK_BA
#ifdef CHECK_BA
      if (i > 0)
      {
        ASSERT(cbid_sz[i] >= cbid_sz[i - 1], "calcba");
        ASSERT(cask_sz[i] >= ask_sz[i - 1], "calcba");
        ASSERT(bid_px[i] < bid_px[i - 1], "calcba");
        ASSERT(ask_px[i] > ask_px[i - 1], "calcba");
      }
#endif
    }

    // if ((lowpx) || (mxpx))
    //   return;
  };

  calc_ba();

  frame::mda::msg::data_pay_load *payload =
      const_cast<frame::mda::msg::data_pay_load *>(got_payload.get());

  payload->point_.bid_px = bid_px;  // #todo dont copy
  payload->point_.ask_px = ask_px;
  payload->point_.bid_sz = bid_sz;
  payload->point_.ask_sz = ask_sz;

  auto &pt = payload->point_;
  auto bo = pt.ask_px[0] - pt.bid_px[0];
  bool locked_or_x = bo <= 0;
  bool small_bid = pt.bid_px[0] <= 10;
  bool large_ask = pt.ask_px[0] >= maxprice;
  bool baddata = locked_or_x || small_bid || large_ask;
  pt.baddata = baddata;

#define PRINTARR(arr)                             \
  cerr << "dbg: ";                                \
  cerr << #arr << " " << endl;                    \
  for (std::size_t kk = 0; kk < arr.size(); kk++) \
    cerr << arr[kk] << ",";                       \
  cerr << endl;

#define COPY_FROM_PLD_TO_POINT(var) \
  payload->point_.var = payload->var

  COPY_FROM_PLD_TO_POINT(mev);
  COPY_FROM_PLD_TO_POINT(action);
  COPY_FROM_PLD_TO_POINT(side);
  //payload->point_.px = payload->px.to_int();
  COPY_FROM_PLD_TO_POINT(side);
  COPY_FROM_PLD_TO_POINT(sym);
  //COPY_FROM_PLD_TO_POINT(sz);
  //COPY_FROM_PLD_TO_POINT(eoe);

// #define DEBUG_ARR
#ifdef DEBUG_ARR
  PRINTARR(bid_px);
  PRINTARR(ask_px);
  PRINTARR(bid_sz);
  PRINTARR(ask_sz);
  PRINTARR(cbid_sz);
  PRINTARR(cask_sz);
  PRINTARR(ba);
  PRINTARR(theta);
  PRINTARR(vwappx);
  PRINTARR(ave_tx_sz);
  PRINTARR(act_ba);
  PRINTARR(presh_ba);
  PRINTARR(rng);
#endif

  ASSERT(!payload->recovery, "cant be recovery");

  if (got_payload->action != en::mt::EXEC)
  {
    have_new_payload = true;
    last_good_payload = got_payload;
  }
  else // this is an exec
  {
    if CHUNLIKELY (got_payload->hndl_tim_epoch && binrec)
      {
        bfile::l3_interval_t l3;
        memset(&l3, 0, sizeof(l3));
        l3.typ = en::l3::INTERVAL;
        l3.venue = got_payload->mkt;
        l3.id = en::intreval::OB_DATA_HANDLER_TRADE;
        l3.t_handler = got_payload->hndl_tim_epoch;
        // l3.t0 = data_handler_enter;
        l3.t1 = chutil::Time::epoch();
        auto msg = new frame::mda::msg::Data();
        msg->l3 = l3;
        binrec->send(msg, this);
      }

    // Track subscribers who have already been sent TradeNotify to prevent duplicates
    std::set<actors::Actor*> notified_subscribers;

    // Send to hiprio_datasubs first
    for (const auto &s : hiprio_datasubs)
    {
      publish_delayed(s, new msg::TradeNotify(got_payload),
                      got_payload->txtim_epoch);
      notified_subscribers.insert(s);
    }

    // Send to bbbosubs, but skip if already notified
    for (const auto &s : bbbosubs)
    {
      if (notified_subscribers.find(s) == notified_subscribers.end()) {
        publish_delayed(s, new msg::TradeNotify(got_payload),
                        got_payload->txtim_epoch);
      }
    }
    
  }

#ifdef PRINTSTATS
  if (print_stats && recent_tx.full())
  {

    if (best_bid > 0 && best_ask && best_ask < maxprice - 10 && best_bid < best_ask)
    {

      ASSERT(best_bid == bid_px[0], "wrong bid");
      ASSERT(best_ask == ask_px[0], "wrong ask");

      // include non eoe payloads
      // but only compute vwap for eoe payloads
      const auto &bp = payload->point_.to_bin();
      points.push_back(bp);
    }
  }
#endif
} // process_market_data

void act::OB::debug_print_book_(int nlevels)
{
  int ba = best_ask;
  int bb = best_bid;
  OBFILE << "[" << bb << " ";
  OBFILE << ba << " ]\n";
  auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
  ASSERT(maxprice - 100 > 0, "maxprice");

  auto prt = [maxprice](const string &pref, int d, const OrderQ *q, int px)
  {
    ASSERT(px < maxprice, "out of range");
    if (q->get_head())
    {
      auto sz_ = q->size_of_book();
      OBFILE << pref << d << " ";
      OBFILE << "[" << px << ":" << sz_.first << ":" << sz_.second << "] ";
      OBFILE << *q << " " << endl;
    }
  };

  for (int px = maxprice - 1; px >= ba; --px)
  {
    auto d = (px - ba) / incr;
    if (d >= nlevels)
      continue;
    auto q = askqs[px];
    prt("A:", d, q, px);
  }
  OBFILE << "=====\n";

  for (int px = bb; px > 0; --px)
  {
    auto d = (bb - px) / incr;
    if (d >= nlevels)
      continue;
    auto q = bidqs[px];
    prt("B:", d, q, px);
  }
  OBFILE << endl;

  if (best_bid > best_ask)
  {
    OBFILE << "inverted " << best_bid << " " << best_ask << std::endl;
    OBFILE.flush();
  }
}

void act::OB::debug_print_book_mbp()
{
  boost::format fmt("%s:%d %8d %8d\n");
  OBFILE << "v------------MBP\n";
  for (int i = 9; i >= 0; i--)
  {
    fmt.clear();
    fmt % "A" % i % mbp_ask_px[i] % mbp_ask_sz[i];
    OBFILE << fmt.str();
  }
  for (std::size_t i = 0; i < 10; i++)
  {
    fmt.clear();
    fmt % "B" % i % mbp_bid_px[i] % mbp_bid_sz[i];
    OBFILE << fmt.str();
  }
  OBFILE << "^------------MBP\n";
}

void act::OB::debug_print_book(unsigned long long id, uint64_t tim, char msg, en::mt typ,
                               en::bs side, int px, int sz)
{
  OBFILE << "sym=" << sym << " id=" << id << " id=" << mda::OrderID::id(id) << " t=" << tim << " " << msg << " "
         << en::to_string(typ)
         << " " << side
         << " px: " << px << " sz: " << sz << " ";

  debug_print_book_(100);
}

void act::OB::end()
{
}

void act::OB::check_bbbo()
{
  if (!debug)
    return;

  // check we don't have size of 0 on the inside
  const OrderQ *aq = askqs[best_ask];
  const OrderQ *bq = bidqs[best_bid];

  ASSERT(aq, "no askq");
  ASSERT(bq, "no bidq");

  //  pair<int, int> bs = bq->size_of_book();
  //  pair<int, int> as = aq->size_of_book();

  if (best_bid > 10)
  {
    ASSERT(!bq->isempty(), "bidq is empty");
    // ASSERT(bs.second>0,"bid is zero");
  }

  auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
  ASSERT(maxprice - 10 > 0, "maxprice");
  if (best_ask < maxprice - 10)
  {
    ASSERT(!aq->isempty(), "askq is empty");
    ASSERT(best_ask < maxprice - 10, "ask is zero");
  }

  // No bids above the best bid, no asks below the best ask.
  //
  // These loops used to run to the end of the ladder while indexing a FIXED
  // level ([best_bid+1] / [best_ask-1]) — the loop variable was unused, so they
  // re-checked one level tens of thousands of times per order. On ESH5
  // (maxpx 25592, 4.08M adds) that is ~10^11 assert evaluations and makes
  // --ob-debug unusable. Index i as intended, and only scan a window past the
  // inside: a violation shows up immediately next to the BBO, and anything
  // deeper is caught on the next update as the BBO walks.
  constexpr uint kCheckWindow = 64;

  // <= bid_hi, not <: with `<` the last level in the window was never checked,
  // and when the window was clamped that level was maxprice-2 -- a real level
  // the ladder addresses.
  const uint bid_hi = std::min<uint>(best_bid + 1 + kCheckWindow, uint(maxprice - 1));
  for (uint i = best_bid + 1; i <= bid_hi; i++)
  {
    ASSERTF(bidqs[i]->isempty_or_allsim(),
            boost::format("bid above the inside: %s level %d (best_bid %d)")
              % get_name() % i % best_bid);
  }

  // best_ask == 0 would make uint(best_ask) - 1 wrap to UINT_MAX and index off
  // the end of askqs. The ASSERT in the caller fires first today, so this has
  // never been reached -- but the underflow is one refactor away from being
  // live, and an out-of-bounds read is a worse failure than the assert it is
  // standing in for.
  if (best_ask <= 1)
    return;
  const uint ask_lo = (uint(best_ask) > kCheckWindow + 1) ? uint(best_ask) - kCheckWindow : 2;
  for (uint i = uint(best_ask) - 1; i >= ask_lo && i > 1; i--)
  {
    ASSERTF(askqs[i]->isempty_or_allsim(),
            boost::format("ask below the inside: %s level %d (best_ask %d)")
              % get_name() % i % best_ask);
  }
}

/**
 * @brief this just adds to the book and forwards gap,
 * end of burst should not be forwarded
 *
 * @param payload
 * @param sender
 */
void act::OB::process_add_or_mod(
    boost::intrusive_ptr<const mda::msg::data_pay_load> payload,
    actors::Actor *sender) noexcept
{

  prev_xoid = payload->ex_order_id;

  ASSERT(payload->is_valid(), "invalid");

  auto handle_eoburst = [&]() {

    if (have_new_payload)
    {

      ASSERT(!last_good_payload->is_sim(), "cant be sim");
      have_new_payload = false;

      if CHUNLIKELY (last_good_payload->hndl_tim_epoch && binrec)
      {
        bfile::l3_interval_t l3;
        memset(&l3, 0, sizeof(l3));
        l3.typ = en::l3::INTERVAL;
        l3.venue = payload->mkt;
        l3.id = en::intreval::OB_DATA_HANDLER_EOB;
        l3.t_handler = last_good_payload->hndl_tim_epoch;
        //l3.t0 = data_handler_enter;
        l3.t1 = chutil::Time::epoch();
        auto msg = new frame::mda::msg::Data();
        msg->l3 = l3;
        binrec->send(msg, this);
      }

      if (!last_good_payload->point_.baddata)
      {
        const_cast<frame::mda::msg::point&>(last_good_payload->point_).shift_zero();
#ifdef USEFASTSEND
        auto msg = new msg::EndOfBurst(
            *last_good_payload);
        for (const auto &s : hiprio_datasubs)
        {
          msg->destination = 0;
          s->fast_send(msg, 0, false);
        }
        delete msg;
#else
        for (const auto &s : hiprio_datasubs)
        {
          publish_delayed(s, new msg::EndOfBurst(last_good_payload),
                          last_good_payload->txtim_epoch);
        }
#endif
      }
      else
      {
        log_dbg("not sending eob because of bad data");
      }

      if (som)
      {
        // add 40 micros
        if (payload->sendtim_epoch > 0)
          som.fast_send(new som::msg::UnStash(payload->sendtim_epoch + 40000), this);
      }

      for (const auto &s : lowprio_datasubs)
      {

        if (!has_pred)
        {
          publish_delayed(s, new msg::EndOfBurst2(
              sym,
              last_good_payload->txtim_epoch,
              last_good_payload->point_.bid_px[0],
              last_good_payload->point_.ask_px[0],
              last_good_payload->point_.bid_sz[0],
              last_good_payload->point_.ask_sz[0],
              0.0,
              last_good_payload->point_.mev,
              last_good_payload->point_.action,
              last_good_payload->point_.side,
              volumefound,
              num_trad), last_good_payload->txtim_epoch);
        }
        else
        {
          publish_delayed(s, new msg::EndOfBurst2(
              sym,
              txtim_epoch,
              best_bid,
              best_ask,
              mbp_bid_sz[0],
              mbp_ask_sz[0],
              0.0,
              en::md::UNI,
              en::mt::UNI,
              en::bs::UNI,
              volumefound), txtim_epoch);
        }
      }
    }
  };

  if (payload->mev == en::md::ADD)
  {
    if (payload->is_sim())
    {
      log_inf("have sim add txtim_epoch: %d, bb: %d, ba: %d, payload: %s",
              txtim_epoch, best_bid, best_ask, payload->to_string());
      ASSERT(sender, "must have sender");
    }
    // #define QDEBUG
#ifdef QDEBUG
    auto side = payload->side;
    auto px = payload->px.to_int();
    auto id = payload->order_ref;
    qvec_t *qv = 0;
    if (side == en::bs::BUY)
    {
      qv = &bidqs;
    }
    else if (side == en::bs::SEL)
    {
      qv = &askqs;
    }
    else
      SNGH;
    auto q = (*qv)[px];
    ASSERT(!q->check_if_there(id), "duplicate add");
#endif
    add(
        payload->txtim_epoch,
        payload->order_ref,
        payload->side,
        payload->px.to_uint(),
        payload->sz,
        sender,
        payload->ot,
        payload->owner,
        payload->ex_order_id,
        payload->mkt);
    handle_eoburst();
  }
  else if (payload->mev == en::md::EOBURST)
  {
    // Allow EOBURST events for bar generation - just handle them
    handle_eoburst();
  }
  else if (payload->mev == en::md::MOD)
  {
    if (payload->is_sim())
    {
      log_inf("have sim mod txtim_epoch: %d, bb: %d, ba: %d, payload: %s",
              txtim_epoch, best_bid, best_ask, payload->to_string());
      ASSERT(sender, "must have sender");
    }
    mod(sender,
        payload->txtim_epoch,
        payload->order_ref,
        payload->action,
        payload->side,
        payload->px.to_uint(),
        payload->sz,
        payload->disp_sz,
        payload->mkt,
        payload->ex_order_id);
    handle_eoburst();
  }
  else if (payload->mev == en::md::CLEAR)
  {
    log_err("got CLEAR message");
    OBFILE << "OBX: got CLEAR message " << sym << std::endl;
    ASSERT(payload->sym == sym, "sym");
    last_tx_tim=0;
    last_recovery_tx_tim=0;
    clear();
  }
  else if (payload->mev == en::md::GAP)
  {
    log_wrn("got GAP");
    OBFILE << "OBX: got GAP" << sym << std::endl;
    for (const auto &s : hiprio_datasubs)
    {
      s->fast_send(new msg::GapDetected(sym), this);
    }
    for (const auto &s : lowprio_datasubs)
    {
      s->send(new msg::GapDetected(sym), this);
    }
  }
  else if (payload->mev == en::md::TEST)
  {
#ifdef TESTBOOK
    if (payload->side == en::bs::BUY)
    {
      auto q = bidqs[payload->px.to_int()];
      auto sz = q->get_orders_in_book();
      ASSERT(payload->sz == sz, "wrong size on buy book");
    }
    else if (payload->side == en::bs::SEL)
    {
      auto q = askqs[payload->px.to_int()];
      auto sz = q->get_orders_in_book();
      ASSERT(payload->sz == sz, "wrong szie on sell side")
    }
    else
      SNGH;
#endif
  }
}

// std::size_t
// act::OB::index_of_darr(const double buckets[], std::size_t n, double val) const noexcept
// {
//   ASSERT(n > 0, "buckets");
//   if (val >= buckets[n - 1])
//     return n;
//   if (val < buckets[0])
//     return 0;
//   double prev_b = 0;
//   auto b = &buckets[0];
//   for (std::size_t i = 0; i < n; i++)
//   {
//     if (i > 0)
//       ASSERT(prev_b <= *b, "bad buckets");
//     if (val < *b)
//       return i;
//     prev_b = *b;
//     b++;
//   }
//   return n;
// }

// void act::OB::index_of_darr_test() const
// {
//   double buckets[] = {0, 1., 2., 3., 4., 5};
//   auto N = sizeof(buckets) / sizeof(buckets[0]);
//   ASSERT(index_of_darr(buckets, N, -1) == 0, "test");
//   ASSERT(index_of_darr(buckets, N, 0) == 1, "test");
//   ASSERT(index_of_darr(buckets, N, .1) == 1, "test");
//   ASSERT(index_of_darr(buckets, N, 6) == 6, "test");
//   ASSERT(index_of_darr(buckets, N, 5) == 6, "test");
//   ASSERT(index_of_darr(buckets, N, 3.5) == 4, "test");
//   ASSERT(index_of_darr(buckets, N, 1.5) == 2, "test");
//   double buckets2[] = {0, 1., 1., 3., 4., 5};
//   auto N2 = sizeof(buckets2) / sizeof(buckets2[0]);
//   ASSERT(index_of_darr(buckets2, N2, .99) == 1, "test");
//   ASSERT(index_of_darr(buckets2, N2, 1.) == 3, "test");
//   ASSERT(index_of_darr(buckets2, N2, 1.1) == 3, "test");
// }

void act::OB::do_canc(
    frame::ob::OrderQ *q,
    frame::ob::Order *o,
    [[maybe_unused]] int32_t px, // todo: cleanup unused
    int32_t sz,
    int32_t dispsz,
    en::mt modtyp,
    [[maybe_unused]] en::x mkt) noexcept
{
  // notify

  // The lookup is a PRESENCE CHECK. Cancel `o`, the order the caller handed us,
  // not whatever the id resolves to.
  //
  // OrderQ::append does `qordermap[n->id] = n` (with its own "TODO: should not
  // allow double insertions"), so two orders sharing an id at one level leave
  // the map pointing at the last one appended. Cancelling the map's entry then
  // destroys a DIFFERENT Order than the caller is standing on -- and
  // fill_stray_sim_orders walks the level holding a cached `next`, which is
  // exactly the pointer that gets freed. The order the caller meant survives in
  // the list but is gone from qordermap, so the exchange's real delete for it
  // later hits mod()'s not-found path and it rests forever.
  //
  // NOT an assert. The divergence this detects is one OrderQ::append can create
  // on its own -- two orders at a level sharing an id leave the map pointing at
  // the second, and cancelling the first erases the entry for the second, so
  // the second survives in the intrusive list with no map entry. The only
  // caller that walks the list rather than the map is fill_stray_sim_orders, so
  // it is that sweep which reaches the orphan, and abort() there takes down a
  // live session over a bookkeeping fault the exchange never caused. The real
  // fix belongs in OrderQ::append; until then, say so loudly and cancel the
  // order the caller is standing on, which is what the list needs regardless.
  auto ptr_ = q->qordermap.find(o->get_id());
  if (ptr_ == q->qordermap.end())
    log_err("do_canc: order %d not in qordermap at this level -- cancelling it "
            "anyway; qordermap and the order list have diverged",
            o->get_id());
  q->canc_notify(o, sz, dispsz, modtyp, &ret_path_);
}

void act::OB::check_handler(const frame::ob::msg::CheckBook *m) noexcept
{

  ASSERT(sym == m->sym, "bad sym");

  auto mxpx = ref::RefData::inst().get_asset(sym)->maxpx;
  if (mxpx <= m->px)
  {
    std::cerr << "OBX: ERR: price to high for CheckBook\n";
    reply(new ob::msg::CheckRes(true));
    return;
  }

  auto check = [this, m](frame::ob::OrderQ *q)
  {
    auto sz = q->get_orders_in_book();
    auto maxprice = ref::RefData::inst().get_asset(sym)->maxpx;
    if (uint32_t(sz) != m->sz && m->px < maxprice)
    {
      std::cerr << "**** size mismatch in CheckBook px: "
                << m->px << " have: " << sz << " expected: " << m->sz
                << " time: " << currtim
                << std::endl;
      debug_print_book_(5);
      SNGH;
      reply(new ob::msg::CheckRes(false));
    }
    else
      reply(new ob::msg::CheckRes(true));
  };

  if (m->side == en::bs::BUY)
  {
    auto q = bidqs[m->px];
    check(q);
  }
  else if (m->side == en::bs::SEL)
  {
    auto q = askqs[m->px];
    check(q);
  }
  else
    SNGH;
}

void act::OB::check_sim_handler(const frame::ob::msg::CheckSim *m) noexcept
{
  ASSERT(sym == m->sym, "bad sym");

  auto mxpx = ref::RefData::inst().get_asset(sym)->maxpx;
  if (mxpx <= m->px)
  {
    std::cerr << "OBX: ERR: price to high for CheckSim\n";
    reply(new ob::msg::CheckRes(true));
    return;
  }

  auto check = [this, m](frame::ob::OrderQ *q)
  {
    auto sz = q->get_sim_in_book();
    if (uint32_t(sz) != m->sz)
    {
      std::cerr << "**** size mismatch in CheckSIM px: "
                << m->px << " have: " << sz << " expected: " << m->sz
                << " time: " << currtim
                << std::endl;
      debug_print_book_(5);
      SNGH;
      reply(new ob::msg::CheckRes(false));
    }
    else
      reply(new ob::msg::CheckRes(true));
  };

  if (m->side == en::bs::BUY)
  {
    auto q = bidqs[m->px];
    check(q);
  }
  else if (m->side == en::bs::SEL)
  {
    auto q = askqs[m->px];
    check(q);
  }
  else
    SNGH;
}

void act::OB::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  std::cerr << get_name() << " shutting down numadd: " << num_add
            << " numexec: " << num_exec
            << " badpx: " << num_bad_px
            << " crossuncross: " << num_cross_recover << std::endl;
  // Say it twice when it is non-zero: the line above is one of many at
  // shutdown, and this one means real exchange orders were deleted to uncross
  // the book, so any fill priced in those windows is suspect.
  if (num_cross_recover)
  {
    // stderr FIRST. Shutdown order is not guaranteed, and the Logger actor may
    // already have processed its own Shutdown -- at which point log_err goes
    // nowhere and the one line that says the book was repaired by deleting real
    // exchange orders is silently lost.
    std::cerr << get_name() << " *** UNCROSSED the book " << num_cross_recover
              << " time(s) this session by cancelling real orders -- prices "
                 "around those events are not trustworthy" << std::endl;
    log_err("%s UNCROSSED the book %llu time(s) this session by cancelling real "
            "orders -- prices around those events are not trustworthy",
            get_name(), (unsigned long long)num_cross_recover);
  }
}

void act::OB::cross_check(boost::intrusive_ptr<const mda::msg::data_pay_load> got_payload)
{
  if (!do_cross_check)
    return;

  // List the orders resting at a level, for diagnostics. This used to also call
  // q->canc_notify_all() — wiping every order at the level, real and sim alike,
  // without recomputing the BBO afterwards — as a way to "uncross" the book.
  // That is a bug: there should be no cross, so deleting real orders hides a
  // reconstruction error and leaves best_bid/best_ask pointing at an emptied
  // level. Diagnostics only now; the crossed branch aborts.
  auto staleorders = [](const ob::OrderQ *q)
  {
    auto h = q->get_head();
    std::string ret = "STALE: ";
    while (h)
    {
      ret += boost::lexical_cast<std::string>(h->get_exordid()) + " ";
      h = static_cast<frame::ob::Order *>(h->next);
    }
    return ret;
  };

  auto mkt = got_payload->mkt;

  if (best_bid > best_ask)
  {

    log_err("ERR BOOK deleting stale book crossed best_bid: %d > best_ask: %d, x: %s",
            best_bid, best_ask, en::to_string(mkt));

    std::cerr << "ERR BOOK  " << get_name()
              << " CROSSED on arrival of ORDER: tim: "
              << got_payload->tim << " timutc:"
              << got_payload->tim << " side: "
              << got_payload->side << " px: "
              << got_payload->px.to_int() << " id: "
              << got_payload->ex_order_id
              << " vsign: " << int(current_mbo.visibility_group)
              << " flags: " << int(current_mbo.order_flags)
              << " volume: " << volumeall
              << std::endl;

    if (got_payload->side == en::bs::BUY)
    {
      const auto &q = askqs[best_ask];
      auto head = q->get_head_no_sim();
      if (head)
      {
        cerr << "crossed the ask side id: "
             << head->get_exordid()
             << staleorders(q)
             << endl;
      }
    }
    else
    {
      const auto &q = bidqs[best_bid];
      auto head = q->get_head_no_sim();
      if (head)
      {
        cerr << "crossed the bid side id: "
             << head->get_exordid()
             << staleorders(q)
             << std::endl;
      }
    }

    crossed_data.push_back(got_payload);

    // Report only — do NOT abort here. cross_check runs on an EOB record, i.e.
    // a PACKET boundary, and a CME transaction can span packets (2025-02-10 tx
    // 1739147206116582643 -> ...116833517), so the book is legitimately crossed
    // at this point mid-transaction. The authoritative no-cross check lives in
    // process_market_data and fires on the first record of the next
    // transaction, once every record of the previous one is applied.
  }

  // for btec the book is often locked temporarily
  // for btec the book is often locked temporarily
#define ALERTLOCKEDBOOK
#ifdef ALERTLOCKEDBOOK
  // Locked, not crossed. This used to test `best_bid <= best_ask`, which is the
  // complement of the crossed branch above and therefore matched EVERY healthy
  // book — and then aborted on it via ERRF. That also made the final else
  // (the crossed-duration report, and the only crossed_data.clear()) dead code.
  // Warn only: per decision a lock is reported, not fatal. The "btec is often
  // locked temporarily" note below refers to BrokerTec, not CME — on CME a
  // locked book is still not expected.
  else if (best_bid == best_ask)
  {
    log_err("BOOK LOCKED %s: best_bid %d == best_ask %d, arriving exordid: %llu",
            get_name(), best_bid, best_ask,
            (unsigned long long)got_payload->ex_order_id);
    #ifdef UNLOCK
    log_err("book locked best_bid: %d == best_ask: %d", best_bid, best_ask);
    std::cerr << "ERR BOOK LOCKED: "
              << got_payload->tim << " "
              << got_payload->ex_order_id
              << std::endl;
    crossed_data.push_back(got_payload);

    if (got_payload->side == en::bs::BUY)
    {
      const auto &q = askqs[best_ask];
      cerr << "LOCK BID order arriving id: "
           << got_payload->ex_order_id << " "
           << got_payload->px.to_int() << " "
           << staleorders(q) << endl;
    }
    else
    {
      const auto &q = bidqs[best_bid];
      cerr << "LOCK ASK order arriving id: "
           << got_payload->ex_order_id << " "
           << got_payload->px.to_int() << " "
           << staleorders(q) << endl;
    }
    #endif
  }
#endif

  else
  {
    if (crossed_data.size() > 0)
    {
      auto first = crossed_data[0];
      auto last = crossed_data[crossed_data.size() - 1];
      auto xdur = last->tim - first->tim;
      if (xdur > 1)
      {
        std::cerr << "OBX ERR: book was crossed for " << xdur << " ns ";
        std::cerr << " start " << first->tim << " end " << last->tim << std::endl;
        log_err("OBX ERR: book was crossed/locked for %d, start: %d, end: %d",
                xdur,
                first->tim,
                last->tim);
      }
      crossed_data.clear();
    }
  }

  //
  // the MBP and MBO are not always in sync
  // usuall MBO moves faster but sometimes its MBP
  //

  // #define CHECKMBOMBP
  // #define TRACEMBOMBP

#ifdef CHECKMBOMBP

  // compare MBO vs MBP

  bool mbpmismatch = false;

#ifdef TRACEMBOMBP
  cerr << best_bid << " " << mbp_bid_px[0] << " --- " << best_ask << " " << mbp_ask_px[0] << endl;
#endif

  if (mbp_bid_px[0] != mbp_ask_px[0])
  {

    if (mbp_bid_px[0] && best_bid != mbp_bid_px[0])
    {
      boost::format fmt("best bid mbo: %d != mbp: %d");
      fmt % best_bid % mbp_bid_px[0];
      log_err(fmt.str());
      cerr << fmt.str() << endl;
      mbpmismatch = true;
    }
    else
    {
      if (got_payload->point_.bid_sz[0] != int(mbp_bid_sz[0])) // fix the cast
      {
        boost::format fmt("best bid sz mbo: %d != mbp: %d");
        fmt % got_payload->point_.bid_sz[0] % mbp_bid_sz[0];
        log_err(fmt.str());
        cerr << fmt.str() << endl;
        mbpmismatch = true;
      }
    }

    if (mbp_ask_px[0] && best_ask != mbp_ask_px[0])
    {
      boost::format fmt("best ask mbo: %d != mbp: %d");
      fmt % best_ask % mbp_ask_px[0];
      log_err(fmt.str());
      cerr << fmt.str() << endl;
      mbpmismatch = true;
    }
    else
    {
      if (got_payload->point_.ask_sz[0] != int(mbp_ask_sz[0])) // fix the cast
      {
        boost::format fmt("best ask sz mbo: %d != mbp: %d");
        fmt % got_payload->point_.ask_sz[0] % mbp_ask_sz[0];
        log_err(fmt.str());
        cerr << fmt.str() << endl;
        mbpmismatch = true;
      }
    }
  }

  if (debug && mbpmismatch)
  {
    debug_print_book_(10);
    debug_print_book_mbp();
  }

#endif
}
