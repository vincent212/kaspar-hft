#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include <boost/unordered/unordered_flat_set.hpp>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/ob/msg/Clear.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "logger/act/Logger.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/ob/msg/GapDected.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "light/qcoord.hpp"
#include "chutil/places.hpp"
#include <boost/property_tree/ptree.hpp>
#include "chutil/Assert.hpp"
#include "light/msg/Start.hpp"
#include "light/msg/Stop.hpp"
#include "light/msg/Set.hpp"
#include "light/msg/GetLightInfo.hpp"
#include "light/msg/LightInfo.hpp"
#include "light/msg/PositionInfo.hpp"
#include "boost/circular_buffer.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "enum/e_names.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ref/Price.hpp"
#include "light/msg/LightSetOffTR.hpp"
#include "db/msg/AddOTRFillRecord.hpp"
#include "light/msg/RegisterLight.hpp"
#include "chutil/price_convert.hpp"
#include "chutil/Time.hpp"


namespace light::act
{

  // CRTP base class for light implementations
  template<typename Derived, en::bs Side>
  struct light22_base : public actors::Actor
  {

    enum TimerIds
    {
      UNSUSPEND = 1,
      RESET_LAST_TRADE_PRICE = 2
    };

    std::string mat;

    bool delay_skip = false;
    bool delay_canc = false;
    int skipped = 0;
    int unskipped = 0;

    char name[256];
    pstring sym;
    pos_pint ssym;
    en::trader owner;
    const frame::ref::Asset *a = 0;

    light::QCoord *qcoord;
    light::PCoord *pcoord;

    uint64_t curr_tx_time = 0;

    cfsmp ob = 0;
    cfsmp super = 0;

    cfsmp timer = 0;
    cfsmp som = 0;

    int skip = 0;
    bool delay = false;

    int num_canc_rej_from_exchange = 0;
    int num_rej_from_exchange = 0;
    int num_canc_rej_unk = 0;

    enum side_to_trade_t
    {
      LONG,
      SHORT,
      BOTH
    };

    side_to_trade_t side_to_trade = BOTH;

    std::set<cfsmp> subs;

    bool fade = false;
    bool orders_suspended = false;

    en::x md_venue = en::x::UNI;
    en::x trading_venue = en::x::UNI;

    place<int, rng_constraint<int, 0, 16>> nlevels;
    place<int, rng_constraint<int, 0, 256>> ord_sz;
    place<int, rng_constraint<int, 0, 17>> stream_incr;

    place<int, rng_constraint<int, 0, 256>> all_orders_max;
    place<int, rng_constraint<int, 0, 256>> lev_orders_max;

    using payload_ptr_t = boost::intrusive_ptr<const frame::mda::msg::data_pay_load>;

    pbool trading;
    pint mmid;
    char *sim_env = 0;

    int last_payload_sym = 0;

    int targetpos = 0;

    msg::LightInfo::last_fill_msg_list_t last_fill_msg;

    msg::PositionInfo latest_position_info;
    bool has_position_info = false;

    bool dumped = false;

    std::string prefix;

    boost::unordered_flat_set<int> cancel_requests;

    std::unordered_map<int, uint64_t> last_order_ts_at_px;

    const char* get_name() const
    {
      return name;
    }

    class ord_info_t
    {
    private:
      int my_ord_id, my_px;
      bool canc = false;
      bool has_value_ = false;

    public:
      void clear()
      {
        my_ord_id = -1;
        my_px = -1;
        canc = false;
        has_value_ = false;
      }
      int get_oid() const
      {
        ASSERT(has_value_, "has_value_");
        return my_ord_id;
      }
      int get_px() const
      {
        ASSERT(has_value_, "has_value_");
        return my_px;
      }
      bool get_canc() const
      {
        ASSERT(has_value_, "has_value_");
        return canc;
      }
      void reset_or_init(int oid, int px)
      {
        has_value_ = true;
        my_ord_id = oid;
        my_px = px;
        canc = false;
      }
      inline bool has_value() const
      {
        return has_value_;
      }
      void set_canc()
      {
        canc = true;
      }
    };

    ord_info_t ord_info;

    const boost::property_tree::ptree &pt;

    uint8_t VG;
    bool dry_run;
    cfsmp db = 0, rm = 0;

    int min_fut_tx_sz = 0;
    int tier = 0;
    int max_num_fades = 0;

    // CRTP: get derived class
    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }

    light22_base(
        const std::string &prefix,
        cfsmp db,
        cfsmp rm,
        const std::string &_name,
        en::trader _owner,
        std::string _sym,
        en::x _mdvenue,
        en::x _trading_venue,
        QCoord *_qcoord,
        PCoord *_pcoord,
        cfsmp _ob,
        cfsmp _super,
        int tier,
        cfsmp _timer,
        cfsmp _som,
        int _mmid,
        const boost::property_tree::ptree &_pt,
        uint8_t VG = 0,
        bool dry_run = false) : prefix(prefix),
                                db(db), rm(rm), pt(_pt),
                                owner(_owner),
                                qcoord(_qcoord),
                                pcoord(_pcoord),
                                ob(_ob),
                                super(_super),
                                tier(tier),
                                timer(_timer),
                                som(_som),
                                VG(VG),
                                dry_run(dry_run),
                                md_venue(_mdvenue),
                                trading_venue(_trading_venue)
    {
      max_num_fades = 1;

      snprintf(name, sizeof(name), "%s", _name.c_str());

      ASSERT(ob, "no order book");

      sim_env = getenv("SIM");

      std::cerr << get_name() << " dry run " << dry_run << std::endl;

      mmid = _mmid;
      sym = _sym;

      nlevels = pt.get<int>("nlevels");
      ord_sz = pt.get<int>("ord_sz");
      auto lev_orders_max_ = pt.get<int>("lev_orders_max");
      lev_orders_max = lev_orders_max_;
      all_orders_max = lev_orders_max * (nlevels + 3);

      std::cerr << get_name() << " nlevels: " << nlevels << std::endl;
      std::cerr << get_name() << " lev_orders_max: " << lev_orders_max << std::endl;
      std::cerr << get_name() << " all_orders_max: " << all_orders_max << std::endl;
      std::cerr << get_name() << " ord_sz: " << ord_sz << std::endl;

      skip = 10;

      a = frame::ref::RefData::inst().get_asset(sym.get());
      ASSERT(a, "no sym");
      ssym = a->id;

      trading = true;

      // Register common message handlers
      MESSAGE_HANDLER(frame::som::msg::CancAck, som_canc_ack_handler);
      MESSAGE_HANDLER(frame::som::msg::Fill, som_fill_handler);
      MESSAGE_HANDLER(frame::som::msg::Reject, som_reject_handler);
      MESSAGE_HANDLER(frame::som::msg::CancReject, som_cancrej_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(frame::ob::msg::Clear, clear_handler);
      MESSAGE_HANDLER(frame::mtim::msg::Alarm, alarm_handler);
      MESSAGE_HANDLER(msg::Start, light_start_handler);
      MESSAGE_HANDLER(msg::Stop, light_stop_handler);
      MESSAGE_HANDLER(msg::Set, light_set_handler);
      MESSAGE_HANDLER(frame::ob::msg::GapDetected, gap_detected_handler);
      MESSAGE_HANDLER(frame::ob::msg::TradeNotify, tradenotify_handler);
      MESSAGE_HANDLER(msg::GetLightInfo, get_light_info_handler);
      MESSAGE_HANDLER(msg::PositionInfo, position_info_handler);
    }

    // Shared implementation: cancel_order
    void cancel_order(bool from_rej = false)
    {
      if (!ord_info.has_value())
      {
        return;
      }
      if (!ord_info.get_canc())
      {
        cancel_requests.insert(ord_info.get_oid());
        if (!from_rej)
        {
          frame::som::msg::Cancel cancel_msg(ord_info.get_oid());
          som->fast_send(&cancel_msg, this);
        }
        ord_info.set_canc();
        auto stbf_ = qcoord->remove_order(ord_info.get_px(), 0, mmid, ord_info.get_oid(), true);
        ASSERT(stbf_ == 0, "did not remove whole order on canc");
        log_inf("cancelling id: %d, work_ords: %d", ord_info.get_oid(), qcoord->total_sz());
      }
      else
      {
        log_err("attempt to cancell twice id: %d", ord_info.get_oid());
      }
      log_inf("done cancel order total sz: %d", qcoord->total_sz());
    }

    // Shared implementation: place_order
    // Returns order id on success, -1 if rate-limited by gunning protection
    int place_order(int price, int sz, uint64_t ts)
    {
      ASSERT(price > 0, "bad price");
      ASSERT(sz > 0, "bad size");

      // Gunning protection: prevent placing orders too frequently at the same price level.
      // This avoids being "gunned" by other participants who detect repeated order placement.
      // Rate limit: 15ms minimum between orders at the same price.
      auto order_placed_ts = chutil::Time::epoch();
      ASSERT(order_placed_ts > 0, "bad order_placed_ts");
      auto p = last_order_ts_at_px.find(price);
      if (p != last_order_ts_at_px.end())
      {
        auto last_ts = p->second;
        ASSERT(last_ts <= order_placed_ts, "bad last_ts");
        auto diff = order_placed_ts - last_ts;
        if (diff < 15000000UL)  // 15ms in nanoseconds
        {
          log_inf("gunning protection: not placing order at px %d, last order was %lu ns ago", price, diff);
          return -1;
        }
      }
      last_order_ts_at_px[price] = order_placed_ts;

      log_inf("placing order ord sz: %d", sz);

      int id = -1;
      {
        if (a->max_limit_order_size > 0 && sz > a->max_limit_order_size)
          sz = a->max_limit_order_size;

        id = frame::som::msg::Order::send(
            prefix,
            ts,
            this,
            som,
            trading_venue,
            ssym,
            sz,
            price,
            Side,
            owner,
            VG);

        ASSERT(!ord_info.has_value(), "order id still has value");
        ord_info.reset_or_init(id, price);
        ASSERT(ord_info.has_value(), "order does not have value");
        qcoord->add_order(price, sz, mmid, id, lev_orders_max);
      }

      log_inf("owner: %s, place_order mmid: %d, px: %d, sz: %d, oid: %d, venue: %s, work_ords: %d",
              en::to_string(owner),
              mmid.get(), price, sz, id,
              en::to_string(trading_venue),
              qcoord->total_sz());

      return id;
    }

    // Shared handlers
    void get_light_info_handler(const msg::GetLightInfo *) noexcept
    {
      auto pos = pcoord->get_position();
      reply(new msg::LightInfo(pos, last_fill_msg));
    }

    void position_info_handler(const msg::PositionInfo *m) noexcept
    {
      latest_position_info = *m;
      has_position_info = true;

      log_inf("position_info: pos=%d, side=%s, last_px=%d",
              m->position,
              en::to_string(m->current_side),
              m->last_px);
    }

    void light_set_handler(const msg::Set *m) noexcept
    {
      if (m->key == msg::Set::SUBSCRIBEONLY)
      {
        ASSERT(m->sender, "no sender");
        log_inf("get_light_info_handler subscribe only: %s", m->sender->get_name());
        subs.insert(m->sender);
      }
      else if (m->key == msg::Set::TARGET_POS)
      {
        targetpos = std::round(m->dval);
        log_inf("setting targetpos to %d", targetpos);
      }
      else if (m->key == msg::Set::LEV_ORDERS_MAX)
      {
        lev_orders_max.reset(std::round(m->dval));
        all_orders_max.reset(lev_orders_max * (nlevels + 1));
        ord_sz.reset(std::round(m->dval));
        log_trd("Set LEV_ORDERS_MAX, lev_orders_max: %d, all_orders_max: %d, ord_sz: %d",
                lev_orders_max.get(), all_orders_max.get(), ord_sz.get());
      }
      else if (m->key == msg::Set::TRADING_OFF)
      {
        log_inf("setting trading to OFF");
        trading_off();
        derived().dump_impl();
      }
      else if (m->key == msg::Set::TRADING_ON)
      {
        log_inf("setting trading to ON");
        trading_on();
        derived().dump_impl();
      }
      else if (m->key == msg::Set::DUMP)
      {
        derived().dump_impl();
      }
      else
      {
        log_err("light_set_handler cannot set value got key: %d", int(m->key));
        std::cerr << "key " << m->key << std::endl;
        SNGH;
      }
    }

    void suspend_orders(int s, int ms)
    {
      orders_suspended = true;
      timer->send(new frame::mtim::msg::AlarmClockSub(s, ms, UNSUSPEND, false), this);
    }

    void tradenotify_handler(const frame::ob::msg::TradeNotify *) noexcept
    {
      // ES futures: trade notifications not used for trading decisions
    }

    void gap_detected_handler(const frame::ob::msg::GapDetected *) noexcept
    {
      log_inf("Gap");
      if (ord_info.has_value() && !ord_info.get_canc())
        cancel_order();
    }

    void light_start_handler(const msg::Start *)
    {
      log_inf("start trading on");
      trading.reset(true);
    }

    void light_stop_handler(const msg::Stop *)
    {
      log_inf("stop trading off");
      trading.reset(false);
    }

    void trading_on()
    {
      log_inf("trading on");
      trading.reset(true);
      derived().dump_impl();
    }

    void trading_off()
    {
      log_inf("trading off");
      trading.reset(false);
      log_inf("trading_off calling cancel_order");
      if (ord_info.has_value() && !ord_info.get_canc())
        cancel_order();
      derived().dump_impl();
    }

    void alarm_handler(const frame::mtim::msg::Alarm *m) noexcept
    {
      if (m->timer_id == UNSUSPEND)
      {
        orders_suspended = false;
      }
      else if (m->timer_id == RESET_LAST_TRADE_PRICE)
      {
        log_inf("resetting last trade price");
        pcoord->set_last_trade_px(0, Side);
      }
      else if (!derived().alarm_handler_impl(m))
      {
        log_err("unknown timer id: %d", m->timer_id);
      }
    }

    // Default implementation - derived classes can override
    bool alarm_handler_impl(const frame::mtim::msg::Alarm *) noexcept
    {
      return false;  // Not handled
    }

    void clear_handler(const frame::ob::msg::Clear *) noexcept
    {
      log_inf("got clear");
      if (ord_info.has_value())
        cancel_order();
      skip = 10;
    }

    void som_cancrej_handler(const frame::som::msg::CancReject *m) noexcept
    {
      if (m->reason == frame::som::msg::CancReject::NOTACKED)
      {
        log_err("sleeping resending canc for NOTACKED reject");
        skip = 50;
        cancel_order(true);
      }
      else if (m->reason == frame::som::msg::CancReject::INTERNALOIDTOCANC)
      {
        log_err("sleeping resending canc for INTERNALOIDTOCANC reject");
        skip = 50;
        som->send(new frame::som::msg::Cancel(m->id), this);
      }
      else if (m->reason == frame::som::msg::CancReject::REJECTONMOD)
      {
        log_err("sleeping resending canc for REJECTONMOD reject");
        skip = 50;
        som->send(new frame::som::msg::Cancel(m->id), this);
      }
      else if (m->reason == frame::som::msg::CancReject::FILLEDALREADY)
      {
        log_inf("not resending canc got can rej but already filled id: %d", m->id);
      }
      else if (m->reason == frame::som::msg::CancReject::CANCELLEDALREADY)
      {
        log_inf("not resending canc got can rej CANCELLEDALREADY id: %d", m->id);
      }
      else if (m->reason == frame::som::msg::CancReject::REJECTEDPREVIOUSLY)
      {
        log_inf("got can rej order was REJECTEDPREVIOUSLY id: %d", m->id);
      }
      else if (m->reason == frame::som::msg::CancReject::FROMEXCHANGE)
      {
        if ((int)m->id == ord_info.get_oid())
        {
          log_inf("got canc reject FROMEXCHANGE id: %d", m->id);
          ord_info.clear();
        }
        else
        {
          log_err("canc got can rej FROMEXCHANGE resending canc id: %d, reason: %d", m->id, m->reason);
        }
        if (num_canc_rej_from_exchange++ > 500)
        {
          log_err("too many rejections from exchange sleeping");
          skip = 1000;
        }
      }
      else if (m->reason == frame::som::msg::CancReject::FROMEXCHANGE_BUSINESSREJECT)
      {
        if ((int)m->id == ord_info.get_oid())
        {
          log_err("got canc rej FROMEXCHANGE_BUSINESSREJECT id: %d", m->id);
          ord_info.clear();
        }
        else
        {
          log_err("got can rej FROMEXCHANGE_BUSINESSREJECT resending canc id: %d, reason: %d", m->id, m->reason);
        }
        if (num_canc_rej_from_exchange++ > 500)
        {
          log_err("too many rejections from exchange sleeping");
          skip = 1000;
        }
      }
      else if (m->reason == frame::som::msg::CancReject::THROTTLE)
      {
        log_err("THROTTLE resending canc got can rej id: %d, reason: %d", m->id, m->reason);
        skip = 1000;
        som->send(new frame::som::msg::Cancel(m->id), this);
      }
      else
      {
        if ((int)m->id == ord_info.get_oid())
        {
          log_inf("got canc rej for unexpected reason id: %d, reason: %d", m->id, m->reason);
          ord_info.clear();
        }
        else
        {
          log_err("got canc reject for unexpected id: %d, reason: %d", m->id, m->reason);
          skip = 100;
        }
        if (num_canc_rej_unk++ > 100)
        {
          log_err("too many rejections from exchange sleeping");
          skip = 1000;
        }
      }
    }

    void som_reject_handler(const frame::som::msg::Reject *m) noexcept
    {
      log_err("order rejected mmid: %d, id: %f", mmid.get(), m->id);
      int stbf = 0;
      if (ord_info.has_value())
      {
        stbf = qcoord->remove_order(ord_info.get_px(), 0, mmid, ord_info.get_oid(), true);
      }
      else
      {
        log_err("no order info for reject id: %f", m->id);
      }
      if (stbf != 0)
        log_err("did not remove whole order on reject");

      if (m->reason == frame::som::msg::Reject::THROTTLE)
      {
        log_err("THROTTLE order rejected mmid: %d, id: %f, reason: %d",
                mmid.get(), m->id, m->reason);
        skip = 50;
        num_rej_from_exchange = 0;
        suspend_orders(0,500);
      }
      else if (m->reason == frame::som::msg::Reject::EXCHLIMIT)
      {
        log_err("POSITION LIMIT order rejected mmid: %d, id: %f, reason: %d",
                mmid.get(), m->id, m->reason);
        skip = 50;
        suspend_orders(0,500);
      }
      else if (m->reason == frame::som::msg::Reject::DISCONNECTED)
      {
        log_err("DISCONNECTED order rejected mmid: %d, id: %f, reason: %d",
                mmid.get(), m->id, m->reason);
        skip = 100;
        suspend_orders(5,0);
      }
      else
      {
        log_err("order rejected mmid: %d, id: %f, reason: %d",
                mmid.get(), m->id, m->reason);
        skip = 5;
      }

      ord_info.clear();
      if (num_rej_from_exchange++ > 100)
      {
        log_err("too many rejections from exchange sleeping: %d", num_rej_from_exchange);
        skip = 1000;
      }
    }

    void som_fill_handler(const frame::som::msg::Fill *m) noexcept
    {
      log_trd("FILLORD fill id: %d, sz: %d, stbf: %d, side: %d, venue: %d, totsz: %d",
              m->id,
              m->sz,
              m->still_to_be_filled,
              int(m->side),
              int(m->venue),
              qcoord->total_sz());

      pcoord->set_last_trade_px(m->px.to_double1(), m->side);

      if (!ord_info.has_value())
      {
        log_err("fill for unknown order id: %d", m->id);
      }
      else
      {
        if ((int)m->id != ord_info.get_oid())
        {
          log_err("fill for wrong order id: %d, expecting: %d", m->id, ord_info.get_oid());
        }
        else
        {
          if (m->owner != owner)
          {
            log_err("got fill for trader %s but expecting %s",
                    en::to_string(m->owner), en::to_string(owner));
          }
          if (!ord_info.get_canc())
          {
            auto stbf_ = qcoord->remove_order(ord_info.get_px(), m->sz, mmid, m->id);
            if (stbf_ != m->still_to_be_filled)
            {
              log_err("stbf mismatch order id: %d, qcoord stbf_: %d, fill stbf: %d",
                      m->id, stbf_, m->still_to_be_filled);
            }
          }
          else
          {
            log_dbg("got fill for a previously cancelled order id: %d", m->id);
          }
        }
      }

      pcoord->add_position(Side, m->sz);
      ASSERT(m->still_to_be_filled >= 0, "single order overfill");
      if (m->still_to_be_filled <= 0)
      {
        ord_info.clear();
      }
      last_fill_msg.push_back(std::make_tuple(*m, curr_tx_time));

      for (auto s : subs)
      {
        if (s)
        {
          auto fill = new frame::som::msg::Fill(*m);
          s->send(fill, this);
        }
      }

      log_inf("curr tot sz for fill id: %d, work_ords: %d", m->id, qcoord->total_sz());
    }

    void som_canc_ack_handler(const frame::som::msg::CancAck *m) noexcept
    {
      log_trd("CANCACKORD got canc ack id: %d, work_ords: %d", m->id, qcoord->total_sz());
      if (!ord_info.has_value())
      {
        log_err("got canc ack but ord_info has no value id: %d", m->id);
        ERRF(boost::format("got canc ack but ord_info has no value id: %d") % m->id);
      }

      auto p = cancel_requests.find(m->id);
      if (p == cancel_requests.end())
      {
        log_err("FORCECANCORD got canc ack for uncancelled order id: %d", m->id);
        auto stbf_ = qcoord->remove_order(ord_info.get_px(), 0, mmid, ord_info.get_oid(), true);
        log_inf("stbf_: %d", stbf_);
      }
      else
      {
        cancel_requests.erase(p);
      }

      if ((int)m->id == ord_info.get_oid())
        ord_info.clear();
      else
      {
        log_err("got canc ack for id: %d, current order id: %d",
                m->id, ord_info.get_oid());
      }

      log_inf("curr tot sz for canc ack id: %d, work_ords: %d", m->id, qcoord->total_sz());
    }

    auto dist_from_best(double midpx, int ord_px, en::bs side)
    {
      if (side == en::bs::BUY)
      {
        return midpx - ord_px;
      }
      else
      {
        return ord_px - midpx;
      }
    };

    auto dist_from_best(int best_px, en::bs side)
    {
      int ord_px = ord_info.get_px();

      int diff;
      if (side == en::bs::BUY)
      {
        diff = best_px - ord_px;
      }
      else
      {
        diff = ord_px - best_px;
      }

      log_dbg("dist_from_best ord_px: %d, best: %d, diff: %e", ord_px, best_px, diff);

      return diff;
    };

    int compute_possible_order_sz(int sz_at_px, int payload_sz) const
    {
      int diff_from_target = compute_diff_from_target();
      int possible_order_sz = std::min(std::min(lev_orders_max.get() - sz_at_px, ord_sz.get()), diff_from_target);
      possible_order_sz = std::min(payload_sz, possible_order_sz);

      if ((sz_at_px > std::max(1, std::abs(diff_from_target/2))) || sz_at_px >= diff_from_target)
      {
        possible_order_sz = 0;
      }

      return possible_order_sz;
    }

    int compute_diff_from_target() const
    {
      auto pos = pcoord->get_position();
      return std::abs(pos - targetpos);
    }

    void shutdown_handler(const actors::msg::Shutdown *)
    {
      log_inf("shutdown");
    }

  };

}
