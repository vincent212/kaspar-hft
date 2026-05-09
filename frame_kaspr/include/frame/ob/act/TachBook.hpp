#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/ob/act/OB_Abstract.hpp"
#include <boost/circular_buffer.hpp>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <array>
#include "frame/mda/msg/Data.hpp"
#include "bfile/r_l3.hpp"
#include <boost/unordered/unordered_flat_map.hpp>
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include "frame/ob/msg/CancNotify.hpp"
#include "frame/ob/msg/Clear.hpp"
#include "frame/ob/msg/GapDected.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ob/msg/EndOfBurst2.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include <boost/format.hpp>
#include "logger/act/Logger.hpp"

//#define DEBUGTACHBOOK
#define UNCROSS_BOOK
//#define TRACKTIME


namespace frame::ob::act
{

  struct TachBook : public actors::Actor, public OB_Abstract
  {

    static constexpr size_t ARRAY_SIZE = 1024 * 100; // 2^10 * 100
    static constexpr int INVALID_BID = -1;
    static constexpr int INVALID_ASK = static_cast<int>(ARRAY_SIZE);

    template<int SIDE>
    class OrderBookSide {
    private:
      std::array<uint, ARRAY_SIZE> levels;
      const TachBook& parent;  // Reference to parent TachBook

      int best;  // best price level: highest for bid (SIDE=0), lowest for ask (SIDE=1)

      void update_best_after_zero(int px) {
        if constexpr (SIDE == 0) { // bid side - search down from px
          best = INVALID_BID;
          for (int i = px - 1; i >= 0; --i) {
            if (levels[i] > 0) {
              best = i;
              break;
            }
          }
        } else { // ask side - search up from px
          best = INVALID_ASK;
          for (int i = px + 1; i < static_cast<int>(ARRAY_SIZE); ++i) {
            if (levels[i] > 0) {
              best = i;
              break;
            }
          }
        }
      }

    public:
      OrderBookSide(const TachBook& p) : parent(p), best(SIDE == 0 ? INVALID_BID : INVALID_ASK) {
        levels.fill(0);
      }

      const char* get_name() const { return "order book side"; }

      void update_sz(int px, int delta) {

        if (px < 0 || px >= static_cast<int>(levels.size())) {
          log_err("Price level out of bounds px: %d maxprice: %d sym: %d name: %s",
                  px, parent.maxprice, parent.sym, parent.name);
          return;
        }

        if (delta < 0 && levels[px] <= std::abs(delta))
        {
          levels[px] = 0; // todo: fix this
        }
        else
        {
          levels[px] += delta;
        }

        if (levels[px] == 0) {
          // Level went to zero, need to update best if this was the best
          if (px == best) {
            update_best_after_zero(px);
          }
        } else {
          // Level has size, check if it's new best
          if constexpr (SIDE == 0) { // bid side - higher is better
            if (px > best) {
              best = px;
            }
          } else { // ask side - lower is better
            if (px < best) {
              best = px;
            }
          }
        }
      }

      uint get_sz(int px) const {
        if (px < 0 || px >= static_cast<int>(levels.size())) {
          return 0;
        }
        return levels[px];
      }

      int get_best() const {
        return best;
      }

      void clear() {
        levels.fill(0);
        best = (SIDE == 0) ? INVALID_BID : INVALID_ASK;
      }

      void clear_at_px(int px) {
        if (levels[px] == 0) return;  // already clear

        levels[px] = 0;

        // Update best if this was the best price
        if (px == best) {
          update_best_after_zero(px);
        }
      }

      const std::array<uint, ARRAY_SIZE>& get_levels() const {
        return levels;
      }
    };

    OrderBookSide<0> bid;  // 0 = bid side
    OrderBookSide<1> ask;  // 1 = ask side
    boost::unordered_flat_map<uint64_t, bfile::l3_mbo_v2_t> orders;
    std::vector<actors::Actor*> hiprio_subs, loprio_subs, aggr_subs, bbbosubs, bbbo_only_subs;

    int prev_best_bid_px = 0, prev_best_ask_px = std::numeric_limits<int>::max();
    int sym;
    int maxprice;
    int px_mult;
    int eob_counter = 0;

    bool have_sec_id = 0;
    int sec_id = 0;

    char name[256];

    const char* get_name() const { return name; }

    boost::intrusive_ptr<frame::mda::msg::data_pay_load> prev_pl=0;


    TachBook(
        int sym) : sym(sym), bid(*this), ask(*this)
    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(frame::mda::msg::Data, data_handler);
      MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);
      MESSAGE_HANDLER(frame::mda::msg::Subscribe, subscribe_handler);
      MESSAGE_HANDLER(frame::ob::msg::BBBOSub, bbosubscribe_handler);

      // OrderBookSide constructor already initializes arrays to zero

      auto a = frame::ref::RefData::get_asset(sym);
      ASSERT(a, "no asset");
      maxprice = a->maxpx;

      snprintf(name, sizeof(name), "TACHOB_%s", frame::ref::RefData::inst().get_asset_name(sym).c_str());

      // Reserve capacity for orders map to avoid rehashing during trading
      // Typical liquid instrument has 5000-10000 active orders
      orders.reserve(16384);  // Pre-allocate for 16K orders (power of 2)

    }

    // Implementation of OB_Abstract virtual functions
    int get_ex_id(int sym) override
    {
      if (!have_sec_id)
      {
        auto a = frame::ref::RefData::get_asset(sym);
        if (a)
        {
          sec_id = a->sec_id;
          have_sec_id = true;
        }
      }
      return sec_id;
    }

    uint get_sym() const override
    {
      return sym;
    }

    int get_ex_sym_id() override
    {
      if (!have_sec_id)
      {
        get_ex_id(sym);
      }
      return sec_id;
    }

    void set_ex_sym_id(uint id) override
    {
      sec_id = id;
      have_sec_id = true;
    }

    void update_bbbo_only_subs() noexcept
    {
      bbbo_only_subs.clear();
      for (auto &sub : bbbosubs)
      {
        if (std::find(hiprio_subs.begin(), hiprio_subs.end(), sub) == hiprio_subs.end())
        {
          bbbo_only_subs.push_back(sub);
        }
      }
    }

    void bbosubscribe_handler(const frame::ob::msg::BBBOSub *m) noexcept
    {
      ASSERT(m->sender, "no sender");
      bbbosubs.push_back(m->sender);
      update_bbbo_only_subs();
    }

    void start_handler(const actors::msg::Start *m) noexcept
    {
      (void)m;
    }

    void shutdown_handler(const actors::msg::Shutdown *m) noexcept
    {
      (void)m;
    }

    void get_handler(const frame::cons::msg::Get *m) noexcept
    {
      (void)m;
    }

    void subscribe_handler(const frame::mda::msg::Subscribe *m) noexcept
    {
      ASSERT(m->sender, "no sender");

      auto sender_str = m->sender->get_name();
      bool sender_has_aggr = sender_str && boost::algorithm::icontains(std::string(sender_str), "aggr");
      bool sender_is_mtd = sender_str && boost::algorithm::icontains(std::string(sender_str), "MTD");
      bool sender_is_timer = sender_str && boost::algorithm::icontains(std::string(sender_str), "Timer");
      bool sender_is_super = sender_str && boost::algorithm::icontains(std::string(sender_str), "super");
      bool sender_allowed = (sender_has_aggr || sender_is_mtd || sender_is_timer) && !sender_is_super;

      ASSERTF(sender_allowed, boost::format("Only aggregators, MTD, or Timer (not super) can subscribe to TachBook, got: %1%") % sender_str);

      if (m->prio == frame::mda::msg::Subscribe::AGGR) // for fast send
      {
        ERRF(boost::format("no aggr sub allowed from: %1%") % sender_str);
        // Check if sender already exists in aggr_subs
        auto it_aggr = std::find(aggr_subs.begin(), aggr_subs.end(), m->sender);
        ASSERT(it_aggr == aggr_subs.end(), "subscriber already exists in aggr_subs");
        // Check if sender already exists in hiprio_subs
        auto it_hi = std::find(hiprio_subs.begin(), hiprio_subs.end(), m->sender);
        ASSERT(it_hi == hiprio_subs.end(), "subscriber already exists in hiprio_subs");
        // Check if sender already exists in loprio_subs
        auto it_lo = std::find(loprio_subs.begin(), loprio_subs.end(), m->sender);
        ASSERT(it_lo == loprio_subs.end(), "subscriber already exists in loprio_subs");
        aggr_subs.push_back(m->sender);
      }
      else if (m->prio == frame::mda::msg::Subscribe::LOW)
      {
        ASSERTF(sender_is_mtd||sender_is_timer, boost::format("no low prio sub allowed from: %1%") % sender_str);
        // Check if sender already exists in hiprio_subs
        auto it_hi = std::find(hiprio_subs.begin(), hiprio_subs.end(), m->sender);
        ASSERT(it_hi == hiprio_subs.end(), "subscriber already exists in hiprio_subs");
        loprio_subs.push_back(m->sender);
      }
      else if (m->prio == frame::mda::msg::Subscribe::HI)
      {
        ASSERTF(sender_has_aggr, boost::format("no hi prio sub allowed from: %1%") % sender_str);
        // Check if sender already exists in loprio_subs
        auto it_lo = std::find(loprio_subs.begin(), loprio_subs.end(), m->sender);
        ASSERT(it_lo == loprio_subs.end(), "subscriber already exists in loprio_subs");
        hiprio_subs.push_back(m->sender);
        update_bbbo_only_subs();
      }
      else
      {
        ERRF(boost::format("invalid sub prio from: %1%") % sender_str);
      }
    }

    auto is_add(const bfile::l3_mbo_v2_t &mbo) const noexcept
    {
      return mbo.orderUpdateAction == 0;
    };

    auto is_canc(const bfile::l3_mbo_v2_t &mbo) const noexcept
    {
      return mbo.orderUpdateAction == 1;
    };

    auto is_cand(const bfile::l3_mbo_v2_t &mbo) const noexcept
    {
      return mbo.orderUpdateAction == 2;
    };

    auto is_exec(const bfile::l3_mbo_trd_v2_t &) const noexcept
    {
      return true;
    };

    auto have_side(const bfile::l3_mbo_v2_t &mbo) const noexcept
    {
      return mbo.side == '0' || mbo.side == '1';
    };

    auto get_side(const bfile::l3_mbo_v2_t &mbo) const noexcept
    {
      if (mbo.side == '0')
        return en::bs::BUY;
      else if (mbo.side == '1')
        return en::bs::SEL;
      else
        SNGH;
      return en::bs::UNK;
    };

    auto eq(double a, double b) const noexcept
    {
      auto epsilon = std::numeric_limits<double>::epsilon();
      return fabs(a - b) <= ((fabs(a) < fabs(b) ? fabs(b) : fabs(a)) * epsilon);
    };

    void data_handler(const frame::mda::msg::Data *m) noexcept
    {
      if (!have_sec_id)
      {
        auto a = frame::ref::RefData::get_asset(sym);
        ASSERT(a, "no asset");
        if (a->sec_id)
        {
          sec_id = a->sec_id;
          have_sec_id = true;
          px_mult = 1 / a->get_units();
          std::cerr << get_name() << " " << "sec_id=" << sec_id
                    << " have_sec_id=" << std::boolalpha << have_sec_id << std::noboolalpha
                    << std::endl;
        }
      }

      boost::intrusive_ptr<frame::mda::msg::data_pay_load> pl=0;


      if (std::holds_alternative<bfile::l3_mbo_v2_t>(m->l3))
      {

        //
        // create payload to be sent with eob
        //

        const auto &mbo = std::get<bfile::l3_mbo_v2_t>(m->l3);
#ifdef DEBUGTACHBOOK
        std::cerr << get_name() << " " << "MBO: " << mbo << " px=" << (mbo.pxd * px_mult) << std::endl;
#endif
        if (mbo.securityID != sec_id)
          return;

        // handle add
        if (is_add(mbo))
        {
#ifdef DEBUGTACHBOOK
          std::cerr << get_name() << " " << "ADD: px=" << (mbo.pxd * px_mult) << std::endl;
#endif

          // if (mbo.orderID == 127131791135780)
          // {
          //   std::cerr << "DEBUG: got orderID 127131791135780" << std::endl;
          // }

          orders.emplace(mbo.orderID, mbo);
          auto side = get_side(mbo);
          auto px = std::round(mbo.pxd * px_mult);

          if (side == en::bs::BUY)
          {
            bid.update_sz(px, mbo.displayQty);

#ifdef UNCROSS_BOOK
            // Uncross: if new bid >= best ask, clear all ASK levels at or below the new bid
            int best_ask_px = ask.get_best();
            if (best_ask_px != INVALID_ASK && px >= best_ask_px) {
              // Clear all ask levels from best_ask up to and including px
              for (int clear_px = best_ask_px; clear_px <= px; ++clear_px) {
                if (ask.get_sz(clear_px) > 0) {
                  ask.clear_at_px(clear_px);
                  log_dbg("Cleared crossing ask at %d (new bid=%d)", clear_px, px);
                }
              }
            }
#endif
          }
          else if (side == en::bs::SEL)
          {
            ask.update_sz(px, mbo.displayQty);

#ifdef UNCROSS_BOOK
            // Uncross: if new ask <= best bid, clear all BID levels at or above the new ask
            int best_bid_px = bid.get_best();
            if (best_bid_px != INVALID_BID && px <= best_bid_px) {
              // Clear all bid levels from px up to and including best_bid
              for (int clear_px = px; clear_px <= best_bid_px; ++clear_px) {
                if (bid.get_sz(clear_px) > 0) {
                  bid.clear_at_px(clear_px);
                  log_dbg("Cleared crossing bid at %d (new ask=%d)", clear_px, px);
                }
              }
            }
#endif
          }

          pl = frame::mda::msg::data_pay_load::make_payload(
              mbo.handlerendtim,
              side,
              mbo.orderID,
              0, // not using order ref
              sym,
              en::md::ADD,
              en::mt::NONE,
              mbo.displayQty,
              mbo.displayQty,
              mbo.pxd,
              mbo.transactTime,
              mbo.sendingTime,
              mbo.venue,
              mbo.endOfEvent || mbo.lastQuote,
              mbo.recovery);
          pl->hndl_tim_epoch = mbo.handlerendtim;
          pl->txtim_epoch = mbo.transactTime;
          pl->sendtim_epoch = mbo.sendingTime;
        }

        else if (is_canc(mbo))
        {
#ifdef DEBUGTACHBOOK
          std::cerr << get_name() << " " << "CANC: px=" << (mbo.pxd * px_mult) << std::endl;
#endif

          // if (mbo.orderID == 127131791135780)
          // {
          //   std::cerr << "DEBUG: got orderID 127131791135780" << std::endl;
          // }

          auto p = orders.find(mbo.orderID);
          if (p == orders.end())
            return;
          auto &ord = p->second;
          auto side = get_side(ord);
          auto px = std::round(ord.pxd * px_mult);

          if (mbo.displayQty > ord.displayQty || ((mbo.pxd > 0) && !eq(mbo.pxd, ord.pxd)))
          {

// why is there no side sometimes
// ASSERT(have_side(m->l3), "no side on canc replace");

// cancel replace
// is the size >?
#ifdef DEBUGTACHBOOK
            {
              auto old_px = static_cast<int>(std::round(ord.pxd * px_mult));
              auto new_px = static_cast<int>(std::round(mbo.pxd * px_mult));
              std::cerr << get_name() << " " << "CANC_REPLACE: orderID=" << mbo.orderID
                        << " side=" << mbo.side
                        << " old_px=" << old_px
                        << " new_px=" << new_px
                        << " old_sz=" << ord.displayQty
                        << " new_sz=" << mbo.displayQty
                        << std::endl;
            }
#endif
            if (mbo.displayQty > ord.displayQty && eq(mbo.pxd, ord.pxd))
            {
#ifdef DEBUGTACHBOOK
              std::cerr << get_name() << " " << "CANCREPL SZ" << std::endl;
#endif
              // just increase the size
              int sz_delta = mbo.displayQty - ord.displayQty;
              ASSERT(sz_delta > 0, "sz_delta <=0 on canc replace");
              ord.displayQty = mbo.displayQty;
              if (side == en::bs::BUY)
              {
                //ASSERT(bid.get_sz(px) >= 0, "bid size is 0 on canc replace");
                bid.update_sz(px, sz_delta);
              }
              else if (side == en::bs::SEL)
              {
                //ASSERT(ask.get_sz(px) >= 0, "ask size is 0 on canc replace");
                ask.update_sz(px, sz_delta);
              }
            }
            else if (mbo.displayQty == ord.displayQty && !eq(mbo.pxd, ord.pxd))
            {
// cancel at this price and add at the other price
#ifdef DEBUGTACHBOOK
              std::cerr << get_name() << " " << "CANCREPL PX" << std::endl;
#endif
              if (side == en::bs::BUY)
              {
                bid.update_sz(px, -static_cast<int>(ord.displayQty));
                ASSERT(bid.get_sz(px) < 1'000'000, "bid size <0 on canc replace");
                auto new_px = std::round(mbo.pxd * px_mult);
                bid.update_sz(new_px, mbo.displayQty);
                ord.pxd = mbo.pxd;
              }
              else if (side == en::bs::SEL)
              {
                //ASSERT(ask.get_sz(px) > 0, "ask size is 0 on canc replace");
                //ASSERT(ask.get_sz(px) >= mbo.displayQty, "ask size < cancel size");
                ask.update_sz(px, -static_cast<int>(mbo.displayQty));
                auto new_px = std::round(mbo.pxd * px_mult);
                ask.update_sz(new_px, mbo.displayQty);
                ord.pxd = mbo.pxd;
              }
            }
            else if (mbo.displayQty > ord.displayQty && !eq(mbo.pxd, ord.pxd))
            {
// both change
#ifdef DEBUGTACHBOOK
              std::cerr << get_name() << " " << "CANCREPL BOTH" << std::endl;
#endif
              int sz_delta = mbo.displayQty - ord.displayQty;
              ASSERT(sz_delta > 0, "sz_delta <=0 on canc replace");
              if (side == en::bs::BUY)
              {
                //ASSERT(bid.get_sz(px) > 0, "bid size is 0 on canc replace");
                //ASSERT(bid.get_sz(px) >= ord.displayQty, "bid size < cancel size on canc replace");
                bid.update_sz(px, -static_cast<int>(ord.displayQty));
                auto new_px = std::round(mbo.pxd * px_mult);
                bid.update_sz(new_px, mbo.displayQty);
                ord.pxd = mbo.pxd;
                ord.displayQty = mbo.displayQty;
              }
              else if (side == en::bs::SEL)
              {
                //ASSERT(ask.get_sz(px) > 0, "ask size is 0 on canc replace");
                //ASSERT(ask.get_sz(px) >= ord.displayQty, "ask size < cancel size on canc replace");
                ask.update_sz(px, -static_cast<int>(ord.displayQty));
                auto new_px = std::round(mbo.pxd * px_mult);
                ask.update_sz(new_px, mbo.displayQty);
                ord.pxd = mbo.pxd;
                ord.displayQty = mbo.displayQty;
              }
            }
            else if (mbo.displayQty < ord.displayQty && !eq(mbo.pxd, ord.pxd))
            {
// both change (quantity decrease)
#ifdef DEBUGTACHBOOK
              std::cerr << get_name() << " " << "CANCREPL BOTH DECREASE" << std::endl;
#endif
              if (side == en::bs::BUY)
              {
                //ASSERT(bid.get_sz(px) > 0, "bid size is 0 on canc replace");
                //ASSERT(bid.get_sz(px) >= ord.displayQty, "bid size < cancel size on canc replace");
                bid.update_sz(px, -static_cast<int>(ord.displayQty));
                auto new_px = std::round(mbo.pxd * px_mult);
                bid.update_sz(new_px, mbo.displayQty);
                ord.pxd = mbo.pxd;
                ord.displayQty = mbo.displayQty;
              }
              else if (side == en::bs::SEL)
              {
                //ASSERT(ask.get_sz(px) > 0, "ask size is 0 on canc replace");
                //ASSERT(ask.get_sz(px) >= ord.displayQty, "ask size < cancel size on canc replace");
                ask.update_sz(px, -static_cast<int>(ord.displayQty));
                auto new_px = std::round(mbo.pxd * px_mult);
                ask.update_sz(new_px, mbo.displayQty);
                ord.pxd = mbo.pxd;
                ord.displayQty = mbo.displayQty;
              }
            }
            else
            {
              ERR("should not be here in canc replace");
            }
          }
          else
          {
// regular cancel
#ifdef DEBUGTACHBOOK
            std::cerr << get_name() << " " << "REG CANC id: "
                      << ord.orderID
                      << std::endl;
#endif

            if (side == en::bs::BUY)
            {
              int sz_delta = static_cast<int>(ord.displayQty) - static_cast<int>(mbo.displayQty);
              // todo: how can sz_delta be 0?
              // ASSERT(sz_delta > 0, "sz_delta <=0 on canc");
              //ASSERT(bid.get_sz(px) > 0, "bid size is 0 on canc");
              //ASSERT(bid.get_sz(px) >= sz_delta, "bid size < cancel size");
              bid.update_sz(px, -sz_delta);
              ord.displayQty = mbo.displayQty;
            }
            else if (side == en::bs::SEL)
            {
              int sz_delta = static_cast<int>(ord.displayQty) - static_cast<int>(mbo.displayQty);
              // todo:
              // ASSERT(sz_delta > 0, "sz_delta <=0 on canc");
              //ASSERT(ask.get_sz(px) > 0, "bid size is 0 on canc");
              //ASSERT(ask.get_sz(px) >= sz_delta, "bid size < cancel size");
              ask.update_sz(px, -sz_delta);
              ord.displayQty = mbo.displayQty;
            }
          }

          pl = frame::mda::msg::data_pay_load::make_payload(
              mbo.handlerendtim,
              get_side(ord),
              mbo.orderID,
              0, // not using order ref
              sym,
              en::md::MOD,
              en::mt::CANC,
              0,
              mbo.displayQty,
              ord.pxd,
              mbo.transactTime,
              mbo.sendingTime,
              mbo.venue,
              mbo.endOfEvent || mbo.lastQuote,
              mbo.recovery);
          pl->hndl_tim_epoch = mbo.handlerendtim;
          pl->txtim_epoch = mbo.transactTime;
          pl->sendtim_epoch = mbo.sendingTime;
        }

        else if (is_cand(mbo))
        {
#ifdef DEBUGTACHBOOK
          std::cerr << get_name() << " " << "CAND: px=" << (mbo.pxd * px_mult) << std::endl;
#endif

          // if (mbo.orderID == 127131791135780)
          // {
          //   std::cerr << "DEBUG: got orderID 127131791135780" << std::endl;
          // }

          auto p = orders.find(mbo.orderID);
          if (p == orders.end())
            return;
          auto &ord = p->second;
          auto side = get_side(ord);
          auto px = std::round(ord.pxd * px_mult);

          if (px < 0 || px >= ARRAY_SIZE)
          {
              log_err("px out of bounds on cancd: px=%d pxd=%f px_mult=%f orderID=%lu",
                      px, ord.pxd, px_mult, mbo.orderID);
              orders.erase(p);
              return;
          }

          if (side == en::bs::BUY)
          {
              bid.update_sz(px, -static_cast<int>(ord.displayQty));
              ASSERT(bid.get_sz(px) < 10'000'000, "bid size <0 on canc");
          }
          else if (side == en::bs::SEL)
          {
              ask.update_sz(px, -static_cast<int>(ord.displayQty));
              ASSERT(ask.get_sz(px) < 10'000'000, "ask size <0 on canc");
          }

          pl = frame::mda::msg::data_pay_load::make_payload(
              mbo.handlerendtim,
              get_side(ord),
              mbo.orderID,
              0, // not using order ref
              sym,
              en::md::MOD,
              en::mt::CANCD,
              0,
              0,
              ord.pxd,
              mbo.transactTime,
              mbo.sendingTime,
              mbo.venue,
              mbo.endOfEvent || mbo.lastQuote,
              mbo.recovery);
          pl->hndl_tim_epoch = mbo.handlerendtim;
          pl->txtim_epoch = mbo.transactTime;
          pl->sendtim_epoch = mbo.sendingTime;

          orders.erase(p);
        }
        else
        {
          log_err("unknown order update action on mbo cancel: %d, mbo: %s", mbo.orderUpdateAction, to_string(mbo));
        }

#ifdef DEBUGTACHBOOK
        if (!mbo.recovery)
          print_book();
#endif

        if (pl && !mbo.recovery) [[likely]]
        {
          publish_book(mbo.handlerendtim,pl);
        }

      } // mbo

      else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(m->l3))
      {
        const auto &mbot = std::get<bfile::l3_mbo_trd_v2_t>(m->l3);
        // trades do not affect order book
        {
          auto oid = mbot.orderID;
          auto p = orders.find(oid);
          if (p == orders.end())
            return;
          auto &ord = p->second;

          #ifdef DEBUGTACHBOOK
          std::cerr << get_name() << " " << "EXEC: ord: " << ord
          << std::endl;
          #endif

          auto side = get_side(ord);
          // auto px = std::round(ord.pxd * px_mult);

          pl = frame::mda::msg::data_pay_load::make_payload(
              mbot.handlerendtim,
              side,
              mbot.orderID,
              0, // not using order ref
              sym,
              en::md::MOD,
              en::mt::EXEC,
              mbot.lastQty,
              0,
              ord.pxd,
              mbot.transactTime,
              mbot.sendingTime,
              mbot.venue,
              mbot.endOfEvent || mbot.lastTrade,
              false);
          pl->hndl_tim_epoch = mbot.handlerendtim;
          pl->txtim_epoch = mbot.transactTime;
          pl->sendtim_epoch = mbot.sendingTime;

          // initialize point with 0
          pl->point_ = {};

#ifdef DEBUGTACHBOOK
          std::cerr << get_name() << " sending trade notify " << *pl << std::endl;
#endif

          for (auto &sub : aggr_subs)
          {
            frame::ob::msg::TradeNotify msg(pl);
            sub->send(&msg, this);
          }

          // publish to subs send out TradeNotify (avoid duplicates)
          for (auto &sub : hiprio_subs)
          {
            sub->send(new frame::ob::msg::TradeNotify(pl), this);
          }

#ifdef tradenotifyforbbbo
          // also send TradeNotify to bbbo_only_subs (precomputed difference)
          for (auto &sub : bbbo_only_subs)
          {
            sub->send(new frame::ob::msg::TradeNotify(pl), this);
          }
#endif

#ifdef TRACKTIME
          if (mbot.handlerendtim)
          {
            log_tim("TIMESTAMP TACHBOOK_TRD_%s: t_handler=%lu, t1=%lu",
                    get_name(),
                    mbot.handlerendtim,
                    chutil::Time::epoch());
          }
#endif
        }
      }

      else if (std::holds_alternative<bfile::l3_chr_v2_t>(m->l3))
      {
#ifdef DEBUGTACHBOOK
        std::cerr << get_name() << " " << "CHANNEL_RESET: clearing book" << std::endl;
#endif
        // channel reset clear book
        ask.clear();
        bid.clear();
        orders.clear();
        // do we notify subs?
        for (auto &sub : hiprio_subs)
        {
          sub->send(new frame::ob::msg::Clear(sym), this);
        }
        for (auto &sub : loprio_subs)
        {
          sub->send(new frame::ob::msg::Clear(sym), this);
        }
      }

      else if (std::holds_alternative<bfile::l3_gap_v2_t>(m->l3))
      {
#ifdef DEBUGTACHBOOK
        std::cerr << get_name() << " " << "GAP_DETECTED: sym=" << sym << std::endl;
#endif
        // just send gap detected to subs
        for (auto &sub : hiprio_subs)
        {
          sub->send(new frame::ob::msg::GapDetected(sym), this);
        }
        for (auto &sub : loprio_subs)
        {
          sub->send(new frame::ob::msg::GapDetected(sym), this);
        }
      }

      else if (std::holds_alternative<bfile::l3_eob_t>(m->l3))
      {
        log_err("received EOB message, which is unexpected in TachBook from sender: %s", m->sender->get_name());
      }

      else
      {
        // ERRF(boost::format("unknown message typ: %d") % m->l3.index());
        log_dbg("unknown message type: {%d}", m->l3.index());
      }

      // Print book state after processing message
    }

    void clean_book()
    {
      // For arrays, zero entries are already "cleaned" by being zero
      // No need to remove them as we just check for non-zero values
    }

    void print_book() const
    {
      std::cerr << get_name() << " " << "=== Order Book for sym " << sym << " ===" << std::endl;

      // Print ask side (showing non-zero prices)
      std::cerr << get_name() << " " << "ASK:" << std::endl;
      for (int px = static_cast<int>(ARRAY_SIZE) - 1; px >= 0; --px)
      {
        if (ask.get_sz(px) > 0)
        {
          std::cerr << get_name() << " " << "  " << px << " : " << ask.get_sz(px) << std::endl;
        }
      }

      std::cerr << get_name() << " " << "--------" << std::endl;

      // Print bid side (showing non-zero prices, highest to lowest)
      std::cerr << get_name() << " " << "BID:" << std::endl;
      for (int px = static_cast<int>(ARRAY_SIZE) - 1; px >= 0; --px)
      {
        if (bid.get_sz(px) > 0)
        {
          std::cerr << get_name() << " " << "  " << px << " : " << bid.get_sz(px) << std::endl;
        }
      }

      std::cerr << get_name() << " " << "=========================" << std::endl;
    }

    void create_point(boost::intrusive_ptr<frame::mda::msg::data_pay_load> pl)
    {

      // Initialize point arrays to zero
      memset(&pl->point_.bid_px, 0, sizeof(pl->point_.bid_px));
      memset(&pl->point_.ask_px, 0, sizeof(pl->point_.ask_px));
      memset(&pl->point_.bid_sz, 0, sizeof(pl->point_.bid_sz));
      memset(&pl->point_.ask_sz, 0, sizeof(pl->point_.ask_sz));

      // Get best prices directly from OrderBookSide
      int highest_bid_px = bid.get_best();
      int lowest_ask_px = ask.get_best();

      if (lowest_ask_px == INVALID_ASK)
      {
        pl->point_.baddata = true;
        return;
      }
      if (highest_bid_px == INVALID_BID)
      {
        pl->point_.baddata = true;
        return;
      }

#ifdef DEBUGTACHBOOK
      if (highest_bid_px > lowest_ask_px) {
        std::cerr << get_name() << " inverted " << "best bid: " << highest_bid_px << " > best ask: " << lowest_ask_px << std::endl;
      }
      #endif

      // Fill bid levels starting from highest
      int current_px = highest_bid_px;
      for (size_t i = 0; i < NLEVELS; ++i)
      {
        pl->point_.bid_px[i] = current_px;
        pl->point_.bid_sz[i] = bid.get_sz(current_px);
        current_px--;
      }

      // Fill ask levels starting from lowest
      int current_ask_px = lowest_ask_px;
      for (size_t i = 0; i < NLEVELS; ++i)
      {
        pl->point_.ask_px[i] = current_ask_px;
        pl->point_.ask_sz[i] = ask.get_sz(current_ask_px);
        current_ask_px++;
      }

      pl->point_.baddata
      = pl->point_.bid_px[0] >= pl->point_.ask_px[0] ||
      pl->point_.bid_px[0] <= 10 ||
      pl->point_.ask_px[0] >= INVALID_ASK - 10 ||
      pl->point_.bid_sz[0] == 0 ||
      pl->point_.ask_sz[0] == 0 ||
      pl->point_.bid_px[0] == 0 ||
      pl->point_.ask_px[0] == 0;

      pl->point_.sym = pl->sym;
      pl->point_.side = pl->side;
      pl->point_.action = pl->action;
      pl->point_.mev = pl->mev;

      // ASSERTF(point.bid_px[0] <= point.ask_px[0],
      //   boost::format("in book %s, bid %d > ask %d") % get_name() % point.bid_px[0] % point.ask_px[0]);

      if (pl->point_.bid_px[0] > pl->point_.ask_px[0])
      {
        //#define DEBUGTACHBOOKLOCKED
        #ifdef DEBUGTACHBOOKLOCKED

        std::cerr << get_name() << " "
                  << boost::format("WARNING: in book %s, bid %d > ask %d")
                  % get_name()
                  % pl->point_.bid_px[0]
                  % pl->point_.ask_px[0]
                  << std::endl;
        #endif
        log_dbg("in book %s, bid %d > ask %d", get_name(), pl->point_.bid_px[0], pl->point_.ask_px[0]);

      }
    }

    void publish_book([[maybe_unused]] uint64_t hndl_tim_epoch, boost::intrusive_ptr<frame::mda::msg::data_pay_load> pl)
    {

#ifdef DEBUGTACHBOOK
      std::cerr << get_name() << " " << "END_OF_BURST: processing book " << hndl_tim_epoch<< std::endl;
#endif

//#define ALWAYSPUB

      // send end of burst

      create_point(pl);

      if (prev_pl)
      {
        bool is_equal = pl->point_.fast_compare(prev_pl->point_);
        if (is_equal)
        {
          //std::cerr << " no change in book " << get_name() << std::endl;
          return;
        }
      }
      prev_pl = pl;

#ifndef ALWAYSPUB
      // create_point already validated and set baddata flag
      // No need to re-fetch best prices - already validated
      if (pl->point_.baddata)
      {
        return;
      }
#endif

      if (!pl->point_.baddata)
      {

        for (auto &sub : aggr_subs)
        {
          frame::ob::msg::EndOfBurst msg(pl);
          sub->send(&msg, this);
        }

        for (auto &sub : hiprio_subs)
        {
          sub->send(new frame::ob::msg::EndOfBurst(pl), this);
        }

#ifdef TRACKTIME
        if (hndl_tim_epoch)
        {
          log_tim("TIMESTAMP TACHBOOK_PUB_%s: t_handler=%lu, t1=%lu",
                  get_name(),
                  hndl_tim_epoch,
                  chutil::Time::epoch());
        }
#endif

        // Send to low priority subs only every 4th time
        if ((++eob_counter % 4) == 0)
        {
          for (auto &sub : loprio_subs)
          {
            sub->send(new frame::ob::msg::EndOfBurst2(
                sym,
                pl->txtim_epoch,
                pl->point_.bid_px[0],
                pl->point_.ask_px[0],
                pl->point_.bid_sz[0],
                pl->point_.ask_sz[0],
                0.0, // mid
                pl->point_.mev,
                pl->point_.action,
                pl->point_.side,
                0, // volumefound
                0), // num_trad
                      this);
          }
        }
      }
      else
      {
        log_dbg("not sending eob because of bad data");
      }

      // send BBBO
      // Use best prices already computed in create_point
      int bb_ = pl->point_.bid_px[0];
      int ba_ = pl->point_.ask_px[0];
      if ((bb_ != prev_best_bid_px || ba_ != prev_best_ask_px) && (bb_ != ba_))
      {
        for (auto &sub : bbbosubs)
        { // send bbbochg
          auto m = new frame::ob::msg::BBBOChg(
              pl->txtim_epoch,
              pl->mkt,
              pl->side,
              pl->sym,
              bb_,
              ba_);
          sub->send(m, this);
        }
        prev_best_bid_px = bb_;
        prev_best_ask_px = ba_;
      }
    }
  };

}