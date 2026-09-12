#pragma once

#include <random>

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "light/act/light22_base.hpp"

namespace light::act
{

  // Derived light implementation using CRTP
  template <en::bs Side>
  struct light22 : public light22_base<light22<Side>, Side>
  {
    using Base = light22_base<light22<Side>, Side>;
    using payload_ptr_t = typename Base::payload_ptr_t;
    using Base::get_name;

    enum TimerIds
    {
      DELAYED_CANCEL = 100  // Use high number to avoid collision with base class
    };

    // Track the exchange order ID from the market data message that triggered order placement
    uint64_t attached_order_id = 0;

    // Deterministic order placement: place order after every N EOB ADD messages
    // Default is 5 (place after 5 EOB ADD messages)
    int place_after_n_eob = 5;
    int place_rate_bp = 300;        // stochastic placement rate, basis points
    // Delay between the shadowed ("attached") order being hit or pulled and our
    // own cancel going out. A real performance lever, not a detail: cancel too
    // quickly and we give up fills we would have got; too slowly and we wear the
    // adverse selection that took the attached order out. Swept by the
    // experiment, so it must be configurable rather than a literal.
    //
    // Two units, because wall-clock is the wrong clock for this. What eats the
    // order after the attached one leaves is *flow*, not time: N more book
    // events is the same amount of danger whether they arrive in 2 ms at the
    // open or 2 s at lunch. `delayed_cancel_events` counts EndOfBurst messages
    // (one per MDP3 incremental cycle for our instrument) and is the preferred
    // knob; `delayed_cancel_ms` keeps the fixed wall-clock variant so the two
    // can be compared on the same corpus. At most one may be non-zero. Both
    // zero (the default) = cancel immediately, which is the usual case.
    int delayed_cancel_events = 0;
    int delayed_cancel_ms = 0;
    std::mt19937 rng{1};            // per-light, deterministically seeded
    int eob_counter = 0;

    // Maximum distance (in ticks) from best bid/ask to place orders
    static constexpr int max_dist = 4;
    // Maximum distance (in ticks) from best bid/ask before cancelling (more loose than placement)
    static constexpr int max_dist_cancel = 6;

    light22(
        const std::string &prefix,
        cfsmp db,
        cfsmp rm,
        const std::string &_name,
        en::trader _owner,
        std::string _sym,
        en::x _mdvenue,
        en::x _trading_venue,
        light::QCoord *_qcoord,
        light::PCoord *_pcoord,
        cfsmp _ob,
        cfsmp _super,
        int tier,
        cfsmp _timer,
        cfsmp _som,
        int _mmid,
        const boost::property_tree::ptree &_pt,
        uint8_t VG = 0,
        bool dry_run = false)
        : Base(prefix, db, rm, _name, _owner, _sym, _mdvenue, _trading_venue,
               _qcoord, _pcoord, _ob, _super, tier, _timer, _som, _mmid, _pt, VG, dry_run)
    {
      // Read deterministic placement interval from config (default 5 = place after 5 EOB ADD messages)
      place_after_n_eob = this->pt.template get<int>("place_after_n_eob", 5);

      // Stochastic placement rate, in BASIS POINTS of EOB ADD messages acted on.
      // Basis points rather than percent so sub-1% rates are expressible: the
      // experiment sweeps 0.5% / 1% / 3% / 5%, and the previous
      // `std::rand() % 100` could not represent 0.5 at all.
      place_rate_bp = this->pt.template get<int>("place_rate_bp", 300);   // 3%
      delayed_cancel_ms = this->pt.template get<int>("delayed_cancel_ms", 0);
      delayed_cancel_events = this->pt.template get<int>("delayed_cancel_events", 0);
      ASSERT(!(delayed_cancel_ms && delayed_cancel_events),
             "delayed_cancel_ms and delayed_cancel_events are alternatives; set at most one");

      // Per-light deterministic RNG. std::rand() is process-global, unseeded and
      // shared with every other caller, so two runs of the same session placed
      // different orders — which makes the long and short legs of a slippage
      // fire incomparable and nothing reproducible. Seed from the config plus
      // the light's own name so buy/sell and each instrument differ, but any
      // given run repeats exactly.
      {
        const auto seed = this->pt.template get<uint32_t>("rng_seed", 1);
        uint32_t h = seed;
        for (char c : _name) h = h * 31u + uint8_t(c);
        rng.seed(h);
      }

      // Register message handlers for derived implementation
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(frame::ob::msg::EndOfBurst, eob_handler);
    }

    void bbbochg_handler(const frame::ob::msg::BBBOChg *)
    {
      // Not used in this implementation
    }

    // Handle derived class timer IDs
    bool alarm_handler_impl(const frame::mtim::msg::Alarm *m) noexcept
    {
      if (m->timer_id == DELAYED_CANCEL)
      {
        if (this->ord_info.has_value() && !this->ord_info.get_canc())
        {
          // Advance the market clock before cancelling. curr_tx_time was last
          // written by the EOB that STARTED this wait, so using it would stamp
          // the cancel delayed_cancel_ms in the past -- and with the default
          // 1000us wire latency any delay over 1 ms then has a deadline that
          // is already expired, so OB releases the cancel on the next record
          // with no latency at all. That is precisely the bug this branch
          // fixes, re-created one path over. The Timer runs on market time and
          // stamps every Alarm with it (Timer.cpp:110).
          const uint64_t alarm_tim = const_cast<frame::mtim::msg::Alarm *>(m)->currtim._epoch_;
          if (alarm_tim > this->curr_tx_time)
            this->curr_tx_time = alarm_tim;
          log_inf("executing delayed cancel at %lu", this->curr_tx_time);
          this->cancel_order();
        }
        return true;  // Handled
      }
      return false;  // Not handled
    }

    void dump_impl() noexcept
    {
      if (this->dumped)
        return;
      this->dumped = true;
      log_inf("dump light info");
      log_inf("tier: %d, sym: %s, symid: %d, venue: %s",
              this->tier, this->sym.get(), this->ssym.get(), en::to_string(this->trading_venue));
      log_inf("side: %d", int(Side));
      log_inf("trading: %d", this->trading.get());
      log_inf("mmid: %d, trader: %s", this->mmid.get(), en::to_string(this->owner));
      log_inf("nlevels: %d, lev_orders_max: %d, all_orders_max: %d, ord_sz: %d",
              this->nlevels.get(), this->lev_orders_max.get(), this->all_orders_max.get(), this->ord_sz.get());
    }

    void start_handler(const actors::msg::Start *)
    {
      log_inf("subscribing to agg: %s", this->ob->get_name());
      this->ob->send(new frame::mda::msg::Subscribe(frame::mda::msg::Subscribe::HI), this);

      dump_impl();

      if (this->rm)
      {
        log_inf("rm is set (registering)");
        this->rm->send(new msg::RegisterLight(this), this);
      }
      else
      {
        log_inf("rm is not set");
      }

      if (this->db)
      {
        log_inf("db is set");
      }
      else
      {
        log_inf("db is not set");
      }
    }

    void place_if_can_impl(payload_ptr_t payload) noexcept
    {
      auto pos = this->pcoord->get_position();

      //log_inf("place_if_can_impl: pos=%d, targetpos=%d, trading=%d", pos, this->targetpos, this->trading.get());

       if (payload->mkt != this->md_venue)
       {
         log_dbg("Skipping place_if_can_impl due to market mismatch. payload->mkt: %s, md_venue: %s",
                 en::to_string(payload->mkt), en::to_string(this->md_venue));
         return;
       }

       // Check if we are already at or beyond our target position for this side
       // If trading is disabled and we are at/beyond target, skip placement

      if ((!this->trading) && ((Side == en::bs::BUY) & (pos >= this->targetpos))) [[unlikely]]
      {
        return;
      }
      if ((!this->trading) && ((Side == en::bs::SEL) & (pos <= this->targetpos))) [[unlikely]]
      {
        return;
      }

      if (this->delay) [[unlikely]]
      {
        return;
      }

      bool isbid = Side == en::bs::BUY;

      if (isbid && pos >= this->targetpos)
      {
        return;
      }
      else if (!isbid && pos <= this->targetpos)
      {
        return;
      }

      if (this->side_to_trade == Base::LONG && Side == en::bs::SEL)
      {
        this->cancel_order();
        return;
      }
      else if (this->side_to_trade == Base::SHORT && Side == en::bs::BUY)
      {
        this->cancel_order();
        return;
      }

      log_dbg("place_if_can");

      // Check if adding another order would exceed our total working order limit.
      // all_orders_max = lev_orders_max * (nlevels + 3), i.e. max orders across all price levels.
      auto work_ords = this->qcoord->total_sz();
      if (work_ords + this->ord_sz > this->all_orders_max)
      {
        log_inf("have too many working orders work_ords: %d, all_orders_max: %d, ord_sz: %d",
                work_ords, this->all_orders_max.get(), this->ord_sz.get());
        return;
      }

      if (this->md_venue != payload->mkt)
      {
        log_dbg("md_venue == payload->mkt, md_venue: %s, mkt: %s",
                en::to_string(this->md_venue), en::to_string(payload->mkt));
        return;
      }

      // Use the price from the add message that triggered this EOB
      auto bestpx = payload->px.to_int();

      // Check if order price is too far from best bid/ask
      // For BUY: reject if bestpx < best_bid - max_dist (too low)
      // For SEL: reject if bestpx > best_ask + max_dist (too high)
      if constexpr (Side == en::bs::BUY)
      {
        int best_bid = payload->point_.bid_px[0];
        if (bestpx < best_bid - max_dist)
        {
          log_inf("price too far from best bid: bestpx=%d, best_bid=%d, max_dist=%d",
                  bestpx, best_bid, max_dist);
          return;
        }
      }
      else
      {
        int best_ask = payload->point_.ask_px[0];
        if (bestpx > best_ask + max_dist)
        {
          log_inf("price too far from best ask: bestpx=%d, best_ask=%d, max_dist=%d",
                  bestpx, best_ask, max_dist);
          return;
        }
      }

      std::lock_guard<std::recursive_mutex> guard(this->qcoord->qcoord_mutex);

      auto diff_from_target = this->compute_diff_from_target();

      int sz_at_px = this->qcoord->sz_at_px(bestpx);

      if (work_ords >= diff_from_target)
      {
        log_inf("work_ords >= diff_from_target, not placing order work_ords: %d, diff_from_target: %d",
                work_ords, diff_from_target);
        return;
      }

      log_inf("place_or_change_level bestpx: %d, sz_at_px: %d, pos: %d, targetpos: %d",
              bestpx, sz_at_px, pos, this->targetpos);

      auto px_incr = [](en::bs side)
      {
        return (side == en::bs::BUY) ? -1 : 1;
      };

      ASSERT(this->lev_orders_max.get() >= 0, "lev_orders_max must be >= 0");
      ASSERT(sz_at_px >= 0, "sz_at_px must be >= 0");
      // Calculate order size as minimum of:
      //   1. lev_orders_max - sz_at_px: remaining capacity at this price level
      //   2. ord_sz: configured max order size per order
      //   3. diff_from_target: remaining position to reach target (don't over-hedge)
      //   4. payload->sz: market depth size available at this level
      int possible_order_sz = std::min(std::min(this->lev_orders_max.get() - sz_at_px, this->ord_sz.get()), diff_from_target);
      possible_order_sz = std::min(payload->sz, possible_order_sz);

      if ((sz_at_px > std::max(1, std::abs(diff_from_target / 2))) || sz_at_px >= diff_from_target)
      {
        log_inf("not placing at this level as we have too much at this level px: %d, sz_at_px: %d, diff_from_target: %d",
                bestpx, sz_at_px, diff_from_target);
        possible_order_sz = 0;
      }

      // Sanity check: verify local calculation matches the base class helper function.
      // Both compute the same order size logic - this catches any drift between the two.
      auto possible_ord_sz_2 = this->compute_possible_order_sz(sz_at_px, payload->sz);
      ASSERT(possible_ord_sz_2 == possible_order_sz, "possible_ord_sz_2 != possible_ord_sz");

      if (possible_order_sz <= 0)
      {
        log_inf("changing level sz_at_px + act_ord_sz: %d, act_lev_orders_max: %d, pos_adj: %d",
                sz_at_px + this->ord_sz.get(), this->lev_orders_max.get(), diff_from_target);

        bestpx += px_incr(Side);
        return;
      }

      attached_order_id = payload->ex_order_id;

      if(this->qcoord->get_attached_order_id() == attached_order_id)
      {
        log_inf("attached_order_id already set to: %ld", attached_order_id);
        return;
      }

      // Placement mode is a runtime choice, not a compile-time one: this used to
      // be `#ifdef DETERMINISTIC_PLACEMENT`, which meant the deterministic path
      // was unreachable in every build anyone actually produced (nothing defined
      // the macro) and the unit tests that exercise it could not be linked.
      //
      //   place_rate_bp > 0  -> stochastic, act on that many basis points of ADDs
      //   place_rate_bp <= 0 -> deterministic, act on every place_after_n_eob'th ADD
      if (place_rate_bp > 0)
      {
        if (int(rng() % 10000u) >= place_rate_bp)
        {
          log_inf("skipping order placement randomly: eob_counter=%d", eob_counter);
          return;
        }
        log_inf("randomly placing order: eob_counter=%d", eob_counter);
      }
      else
      {
        eob_counter++;
        if (eob_counter < place_after_n_eob)
        {
          log_inf("skipping order placement: eob_counter=%d, place_after_n_eob=%d",
                  eob_counter, place_after_n_eob);
          return;
        }
      }

      // Reset counter after placing order
      eob_counter = 0;

      if (!this->dry_run)
      {

#ifdef TIMTRACE
        auto id = this->place_order(
            bestpx,
            possible_order_sz,
            payload->hndl_tim_epoch);
#else
        auto id = this->place_order(
            bestpx,
            possible_order_sz,
            payload->txtim_epoch);
#endif

        // place_order returns -1 if rate-limited by gunning protection
        if (id < 0)
          return;

        // Record the exchange order ID from the market data message that triggered this order placement
        this->qcoord->set_attached_order_id(attached_order_id);
        this->pending_cancel_eob = 0;

        log_trd("PLACEORD placing order id: %d, bestpx: %d, possisble_order_sz: %d, act_lev_orders_max: %d, diff_from_target: %d, sz_at_px: %d, act_ord_sz: %d",
                id,
                bestpx,
                possible_order_sz,
                this->lev_orders_max.get(), diff_from_target, sz_at_px, this->ord_sz.get());
      }
      else
      {
        log_inf("dry run placing order best_px: %d, possisble_order_sz: %d",
                bestpx, possible_order_sz);

        std::cerr << "DRYPLACEORD "
                  << en::to_string(Side)
                  << " best_px: "
                  << bestpx
                  << ", best32: "
                  << chutil::convert_price_to_32nds(bestpx * this->a->get_units())
                  << ", possisble_order_sz: "
                  << possible_order_sz
                  << std::endl;

        log_trd("DRYPLACEORD bid0: %d, ask0: %d, px: %d, px32: %s, possisble_order_sz: %d, act_lev_orders_max: %d, diff_from_target: %d, sz_at_px: %d, ord_sz: %d, venue: %d",
                payload->point_.bid_px[0],
                payload->point_.ask_px[0],
                bestpx,
                chutil::convert_price_to_32nds(bestpx * this->a->get_units()),
                possible_order_sz,
                this->lev_orders_max.get(), diff_from_target, sz_at_px, this->ord_sz.get(),
                int(this->trading_venue));
      }

    }

    void canc_if_must_impl(payload_ptr_t payload, bool delay_skip) noexcept
    {
      auto pos = this->pcoord->get_position();
      auto curr_sz = this->qcoord->sz_at_px(this->ord_info.get_px());

      auto hedge_pos_size_breach = [this](int curr_sz, int pos)
      {
        auto diff_from_target = std::abs(pos - this->targetpos);
        return curr_sz > diff_from_target;
      };

      auto directional_position_breach = [this](int curr_sz)
      {
        ASSERT(curr_sz > 0, "currsz");
        return curr_sz > this->lev_orders_max;
      };

      auto too_far_from_inside = [this, payload]()
      {
        int ord_px = this->ord_info.get_px();
        if constexpr (Side == en::bs::BUY)
        {
          int best_bid = payload->point_.bid_px[0];
          return ord_px < best_bid - max_dist_cancel;
        }
        else
        {
          int best_ask = payload->point_.ask_px[0];
          return ord_px > best_ask + max_dist_cancel;
        }
      };

      log_dbg("pos: %d", pos);

      bool cancelled = false;

      // attached_order_id check moved to eob_handler
      if (pos == 0)
      {
        log_trd("CANCORD id: %d, cancelling because position is 0", this->ord_info.get_oid());
        this->pcoord->incr_zero_pos();
        this->cancel_order();
        cancelled = true;
      }
      else if (Side == en::bs::BUY && pos >= this->targetpos)
      {
        log_trd("CANCORD id: %d, cancelling because pos >= targetpos", this->ord_info.get_oid());
        this->pcoord->incr_zero_pos();
        this->cancel_order();
        cancelled = true;
      }
      else if (Side == en::bs::SEL && pos <= this->targetpos)
      {
        log_trd("CANCORD id: %d, cancelling because pos <= targetpos", this->ord_info.get_oid());
        this->pcoord->incr_zero_pos();
        this->cancel_order();
        cancelled = true;
      }
      else if (hedge_pos_size_breach(curr_sz, pos))
      {
        log_trd("CANCORD id: %d, cancelling because we could over hedge", this->ord_info.get_oid());
        this->pcoord->incr_hedge_pos_size_breach();
        this->cancel_order();
        cancelled = true;
      }
      else if (directional_position_breach(curr_sz))
      {
        log_trd("CANCORD id: %d, cancelling because too many orders at this level", this->ord_info.get_oid());
        this->pcoord->incr_directional_pos_breach();
        this->cancel_order();
        cancelled = true;
      }
      else if (delay_skip)
      {
        log_inf("id: %d, cancelling because delay_skip is set", this->ord_info.get_oid());
        this->pcoord->incr_delay_skip();
        this->cancel_order();
        cancelled = true;
      }
      else if (too_far_from_inside())
      {
        log_trd("CANCORD id: %d, cancelling because order too far from inside market", this->ord_info.get_oid());
        this->cancel_order();
        cancelled = true;
      }

      if (cancelled)
      {
        log_inf("STATS canc_if_must: hedge_breach=%d, dir_breach=%d, zero_pos=%d, delay_skip=%d, attached_order_id_match=%d",
                this->pcoord->hedge_pos_size_breach,
                this->pcoord->directional_pos_breach,
                this->pcoord->zero_pos,
                this->pcoord->delay_skip,
                this->pcoord->attached_order_id_match);
      }
    }

    void eob_handler(const frame::ob::msg::EndOfBurst *m) noexcept
    {

      //log_inf("eob");

      auto sym = m->payload->point_.sym;
      auto sym_a = frame::ref::RefData::inst().get_asset(sym);
      auto msg_sector = sym_a->mnemonic;

      auto pld = m->payload;
      this->curr_tx_time = pld->txtim_epoch;
      const auto &pt = m->payload->point_;

      ASSERT(pld->is_valid(), "not valid");
      ASSERT(!pld->recovery, "cant be recovery");

#define RET                                                     \
  if (this->ord_info.has_value() && !this->ord_info.get_canc()) \
    this->cancel_order();                                       \
  return

      if (m->payload->mkt != this->md_venue)
      {
        return;
      }

      // Event-count cancel delay: one tick per EOB on our instrument. Armed when
      // the attached order went away (below); cleared by cancel_order() and by a
      // fresh placement, so a countdown can never outlive the order it was armed
      // for.
      if (this->pending_cancel_eob > 0 && --this->pending_cancel_eob == 0)
      {
        if (this->ord_info.has_value() && !this->ord_info.get_canc())
        {
          log_inf("executing delayed cancel after %d events", delayed_cancel_events);
          this->cancel_order();
        }
      }

      if (this->skip-- > 0)
      {
        log_trc("skipping %d", this->skip);
        RET;
      }
      else
      {
        this->skip = 0;
      }

      ASSERT(m->payload->action != en::mt::EXEC, "cannot be exec");

      if (pt.baddata) [[unlikely]]
      {
        log_inf("bad data");
        RET;
      }
      if (m->payload->mev == en::md::CLEAR) [[unlikely]]
      {
        log_inf("got clear");
        RET;
      }
      if (m->payload->recovery) [[unlikely]]
      {
        log_inf("recovery");
        RET;
      }

      // Check if attached order was hit/cancelled - schedule delayed cancel
      if (this->ord_info.has_value() && !this->ord_info.get_canc() &&
          pld->ex_order_id == this->attached_order_id && this->attached_order_id != 0)
      {
        log_trd("CANCORD id: %d, attached_order_id %lu gone",
                this->ord_info.get_oid(), attached_order_id);
        this->pcoord->incr_attached_order_id_match();
        this->attached_order_id = 0;

        if (delayed_cancel_events > 0)
        {
          this->pending_cancel_eob = delayed_cancel_events;
          return;              // deliberately not RET: RET cancels immediately
        }
        if (delayed_cancel_ms > 0)
        {
          this->timer->send(new frame::mtim::msg::AlarmClockSub(
                                delayed_cancel_ms / 1000, delayed_cancel_ms % 1000,
                                this->DELAYED_CANCEL, false),
                            this);
          return;              // ditto
        }
        RET;                   // no delay configured: cancel now
      }

      // No order exists and signal is ADD -> place new order
      if (!this->ord_info.has_value() && pld->is_add())
      {
        log_trc("no order info");
        this->unskipped++;
        place_if_can_impl(pld);
      }
      // Order exists, cancel not yet sent, and signal is CANC -> cancel the order
      else if (this->ord_info.has_value() && !this->ord_info.get_canc() && pld->is_canc())
      {
        log_trc("ord_info id: %d, canc: %d", this->ord_info.get_oid(), this->ord_info.get_canc());
        canc_if_must_impl(pld, this->delay_canc);
      }
      // Cancel already sent, waiting for ack - or signal doesn't match current state
      else
      {
        if (this->ord_info.has_value())
          log_inf("waiting for canc ack id: %d", this->ord_info.get_oid());
      }
    }
  };

}
