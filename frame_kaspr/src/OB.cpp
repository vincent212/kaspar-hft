
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <set>
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

// #define NOXMKT

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
    q->canc_notify_all();
  }
  for (std::size_t i = 0; i < askqs.size(); i++)
  {
    auto q = askqs[i];
    if (q->isempty())
      continue;
    q->canc_notify_all();
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
            sender->send(rej_msg, this);
#ifdef TRACEORDERS
            cerr << ">>> REJONARR: " << short_id << " " << sim_tim.date << " " << sim_tim.ns << endl;
#endif
            return true;
          }
#endif
          auto o = new Order(txtim, id, exordid, sym, side, sz, sender, owner);
          OrderQ::fill_notify_s(o, sz, -1, best_ask, en::mt::EXECD, txtim);
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
            sender->send(rej_msg, this);
            return true;
          }
#endif
          auto o = new Order(txtim, id, exordid, sym, side, sz, sender, owner);
          OrderQ::fill_notify_s(o, sz, -1, best_bid, en::mt::EXECD, txtim);
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
  auto fill_stray_sim_orders = [this, px, side, maxprice, txtim]() noexcept
  {
    //
    // fill stray sim orders when a real add bid arrives but there are sim
    // orders that are lower in price
    //
    if ((side == en::bs::BUY) * (px > best_bid))
    {
      int tmp_px = best_bid;
      while (tmp_px <= px)
      {
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
#ifdef NOXMKT
          else if (!stray_o->issim() && stray_o->get_side() == en::bs::SEL)
          {
            do_canc(q_i, stray_o, tmp_px, stray_o->get_sz(), 0, en::mt::CANCD, venue);
#ifdef TRACEORDERS
            std::cerr << ">>> CANCSTRAY: " << mda::OrderID::id(stray_o->get_id()) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
          }
#endif
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
    else if ((side == en::bs::SEL) * (px < best_ask))
    {
      int tmp_px = best_ask;
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
#ifdef NOXMKT
          else if (!stray_o->issim() && stray_o->get_side() == en::bs::BUY)
          {
            do_canc(q_i, stray_o, tmp_px, stray_o->get_sz(), 0, en::mt::CANCD, venue);
#ifdef TRACEORDERS
            std::cerr << ">>> CANCSTRAY: " << mda::OrderID::id(stray_o->get_id()) << " " << ctim.date << " " << ctim.ns << std::endl;
#endif
          }
#endif
          stray_o = next;
        }
        tmp_px -= 1;
        if (tmp_px <= 0)
          break;
      }
    }
  };

#endif

  auto check_for_bo_change = [this, side, px, txtim, sim](en::x venue)
  {
    ASSERT(!sim, "cant be sim");

    bool bochg = false, bbchg = false;

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
        bochg = true;
      }
#ifdef NOXMKT
      auto q = askqs[best_ask];
      while (q->isempty_or_allsim() && best_ask < maxprice - 1)
      {
        best_ask++;
        q = askqs[best_ask];
      }
#endif
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
#ifdef NOXMKT
      auto q = bidqs[best_bid];
      while (q->isempty_or_allsim() && best_bid > 0)
      {
        best_bid--;
        q = bidqs[best_bid];
      }
#endif
    }
    else
      SNGH;

    if (bbchg + bochg)
    {
      if ((best_bid > 0) + (best_ask > 0))
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

  auto mod_order_that_is_not_found = [sim, this, modtyp, sender, id]()
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
        sender->send(f, this);
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
    q->fill_notify(s, s->get_sz(), -1, px, en::mt::EXECD, tim);
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
    q->fill_notify(s, fillsz, -1, px, en::mt::EXEC, tim);
    ASSERT(s->get_sz() > 0, "partial fill but size is < 0");
  }
}

void act::OB::notifybbbosubs(
    uint64_t txtim,
    en::bs side,
    en::x venue)
{
  // Throttle: minimum 100ms between notifications (max 10/sec)
  // txtim is in nanoseconds
  const uint64_t MIN_INTERVAL_NS = 100'000'000;  // 100ms in nanoseconds

  if (last_bbo_notify_tim != 0) {
    uint64_t elapsed = txtim - last_bbo_notify_tim;
    if (elapsed < MIN_INTERVAL_NS) {
      // Too soon, skip this notification
      return;
    }
  }

  if (best_bid == best_ask)
  {
    // Market is locked (bid == ask) or inverted (bid > ask), skip notification
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
    c->send(m, this);
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

  if (is_add(m->l3))
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
        mbo.endOfEvent || mbo.lastQuote,
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
        mbot.endOfEvent || mbot.lastTrade,
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
  else if (is_canc(m->l3))
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
          mbo.endOfEvent || mbo.lastQuote,
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
          mbo.endOfEvent || mbo.lastQuote,
          mbo.recovery);
      pl->txtim_epoch = txtim_epoch;
      pl->sendtim_epoch = sendtim_epoch;
      pl->hndl_tim_epoch = handlerendtim;

      process_slated_exec(ordref, pl);
    }
  }
  else if (is_cand(m->l3))
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
        mbo.endOfEvent || mbo.lastQuote,
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
    auto order_engine_arrive_time = order_leave_time + std::max(40, delay) * 1000; // 40 us delay
    if (order_engine_arrive_time < to_proc->tim)
    {

#if defined(TRACEORDERS) || defined(DBGDELQ)
      cerr << "POP order "
           << mda::OrderID::id(p_->order_ref)
           << " sent at " << order_leave_time.to_string()
           << " arrved at " << order_engine_arrive_time.to_string()
           << " because tx nowis " << to_proc->tim.to_string()
           << " d = " << delay << endl;
#endif

      log_dbg("popping order id: %d sent at: %d, arrived at: %d, because time now is: %d, del: %d",
              mda::OrderID::id(p_->order_ref),
              order_leave_time,
              order_engine_arrive_time,
              to_proc->tim,
              delay);

      // we let this order through

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
           << " delay = " << delay
           << " current sent time is "
           << to_proc->send_tim.to_string()
           << endl;
#endif

      log_trc("NO POP order id: %d, sent at: %s, bacause tx time is: %s, del: %d, current time: %s",
              mda::OrderID::id(p_->order_ref), p_->send_tim.to_string(), to_proc->tim.to_string(), delay, to_proc->send_tim.to_string());

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

#ifdef NOXMKT
  ASSERT(best_bid <= best_ask, "book locked");
#endif

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
      s->send(new msg::TradeNotify(
          got_payload), this);
      notified_subscribers.insert(s);
    }

    // Send to bbbosubs, but skip if already notified
    for (const auto &s : bbbosubs)
    {
      if (notified_subscribers.find(s) == notified_subscribers.end()) {
        s->send(new msg::TradeNotify(
            got_payload), this);
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

  for (uint i = best_bid + 1; i < uint(maxprice - 1); i++)
  {
    auto q2 = bidqs[best_bid + 1];
    ASSERT(q2->isempty_or_allsim(), "must be empty");
  }

  for (uint i = best_ask - 1; i > 1; i--)
  {
    auto q3 = askqs[best_ask - 1];
    ASSERT(q3->isempty_or_allsim(), "must be empty");
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
          s->send(new msg::EndOfBurst(
              last_good_payload), this);
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
          s->send(new msg::EndOfBurst2(
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
              num_trad), this);
        }
        else
        {
          s->send(new msg::EndOfBurst2(
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
              volumefound), this);
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

  bool order_found = false;
  Order *order_ptr = nullptr;
  auto ptr_ = q->qordermap.find(o->get_id());
  if (ptr_ != q->qordermap.end())
  {
    order_ptr = ptr_->second;
    order_found = true;
  }
  //Order *const *order_ptr;
  //auto order_found = q->ordermap.get(o->get_id(), order_ptr);
  ASSERT(order_found, "order not found");
  q->canc_notify(order_ptr, sz, dispsz, modtyp);
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
            << " numexec: " << num_exec << std::endl;
}

void act::OB::cross_check(boost::intrusive_ptr<const mda::msg::data_pay_load> got_payload)
{

  ERRF(boost::format("CROSSCHECK>>>> book locked best_bid: %d == best_ask: %d") % best_bid % best_ask);


  if (!do_cross_check)
    return;

  auto staleorders = [got_payload](ob::OrderQ *q)
  {
    auto h = q->get_head();
    std::string ret = "STALE: ";
    while (h)
    {
      ret += boost::lexical_cast<std::string>(h->get_exordid()) + " ";
      h = static_cast<frame::ob::Order *>(h->next);
    }
    q->canc_notify_all();
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

    // SNGH;

    crossed_data.push_back(got_payload);
  }

  // for btec the book is often locked temporarily
  // for btec the book is often locked temporarily
#define ALERTLOCKEDBOOK
#ifdef ALERTLOCKEDBOOK
  else if (best_bid <= best_ask)
  {
    ERRF(boost::format("book locked best_bid: %d == best_ask: %d") % best_bid % best_ask);
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
