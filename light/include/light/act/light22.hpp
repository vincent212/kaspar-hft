#pragma once

#include <algorithm>
#include <limits>
#include <random>

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
      DELAYED_CANCEL = 100, // Use high number to avoid collision with base class
      AGGR_TTL       = 101  // aggressive order time-to-live: fill or be gone
    };

    // Track the exchange order ID from the market data message that triggered order placement
    uint64_t attached_order_id = 0;
    // True when attached_order_id came from a TRADE (the aggressive path) rather
    // than from a resting ADD. The id is used for TWO unrelated things and only
    // one of them is valid for a cross:
    //   - bank-wide dedup, so two lights do not act on the same event  -> BOTH
    //   - cancel when the anchor leaves the book                       -> PASSIVE ONLY
    // A trade's referenced order is being consumed as we attach to it, so the
    // cancel fires immediately: it killed 293 of 293 aggressive orders before
    // this split.
    bool attached_is_trade = false;

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
    uint64_t place_rate_thresh = 0;  // place_rate_bp * 2^32 / 10000
    int aggr_ttl_ms = 1;             // aggressive order time-to-live, ms; 0 = off
    // The order the TTL alarm was armed for. An alarm can outlive its order --
    // a cross fills in ~40 us and the light places again long before 1 ms is
    // up -- so firing on whatever happens to be in the slot would cancel a
    // healthy NEW order early. Same guard pending_cancel_eob already has for
    // delayed_cancel_events; it was not carried across when AGGR_TTL was added.
    int aggr_ttl_oid = -1;
    int eob_counter = 0;

    // How deep into the book we are willing to rest, in ticks from the touch.
    //
    // This is the binding constraint on how much size can be working at once:
    // at most (max_dist + 1) price levels x lev_orders_max per level. With the
    // 4/5 defaults that is 25 contracts, so a 100-lot parent cannot be worked
    // in one pass and is forced into refill rounds -- which shows up as a
    // longer leg and higher cost, and is a property of THIS configuration
    // rather than of passive execution. It is configurable so the size axis
    // can be separated from the depth cap.
    int max_dist = 4;
    // Looser than placement, so an order is not cancelled the moment the touch
    // ticks away from it. Defaults to max_dist + 2, preserving the original
    // 4/6 relationship at any depth.
    int max_dist_cancel = 6;

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

      // AGGRESSIVE TIME-TO-LIVE, milliseconds. An aggressive order is an IOC in
      // intent: it should fill on arrival or not exist. With no anchor to pull
      // it (see place_if_can_impl) a cross that misses would otherwise rest
      // until it drifts max_dist_cancel ticks away or the leg completes --
      // silently becoming a passive order the arm never asked for. 0 disables.
      aggr_ttl_ms = this->pt.template get<int>("aggr_ttl_ms", 1);

      // Stochastic placement rate, in BASIS POINTS of EOB ADD messages acted on.
      // Basis points rather than percent so sub-1% rates are expressible: the
      // experiment sweeps 0.5% / 1% / 3% / 5%, and the previous
      // `std::rand() % 100` could not represent 0.5 at all.
      place_rate_bp = this->pt.template get<int>("place_rate_bp", 300);   // 3%
      place_rate_thresh = (uint64_t(std::max(0, place_rate_bp)) << 32) / 10000u;
      // Aggressive participation, in basis points of TRADES shadowed. 0 = off,
      // which is the default and the pure shadow algorithm.
      //
      // The config value is the BANK TOTAL and is divided here by the number of
      // lights in the bank, because every light sees every trade and flips its
      // own coin -- four lights each acting on 2% of trades would shadow 8%
      // LITERAL, not divided by the light count.
      //
      // It used to be a bank total divided by nlights_per_side. That was built
      // for a design where aggression rode on the passive lights, and it never
      // worked: a light declines a trade while it already holds a working order
      // (light22_base: `if (ord_info.has_value()) return;`), and at any useful
      // place_rate_bp the passive lights are occupied nearly all the time. The
      // 2026-09-14 grid D run configured 2% of trades and delivered between
      // 2.4% and 0% of that, ranked INVERSELY with placement rate -- the
      // signature of crowding-out.
      //
      // Aggression now runs in its OWN bank: one light per side, place_rate_bp
      // 0, its own QCoord, sharing the passive bank's PCoord. A marketable
      // order clears the slot on arrival rather than resting, so one light
      // sustains the rate. With a fixed bank of one, dividing by the light
      // count is at best a no-op and at worst the trap it used to be, so the
      // number here is the number you get: 200 = 2% of trades.
      this->aggr_participation_bp =
          this->pt.template get<int>("aggr_participation_bp", 0);
      // bp -> 32-bit threshold, once. 10000bp maps to exactly 2^32, which no
      // mt19937 draw can reach, so 100% fires on every trade as intended.
      this->aggr_participation_thresh =
          (uint64_t(std::max(0, this->aggr_participation_bp)) << 32) / 10000u;
      // Logged at OPER so it survives --quiet: which bank a light belongs to is
      // otherwise invisible in a grid run, and "is the passive bank also
      // crossing?" is not a question that should need a source read.
      // std::cerr, not log_opr: this runs in the CONSTRUCTOR and the Logger
      // actor is not started yet, so a logged line here is silently dropped --
      // which is why light22_base:313 dumps its config the same way.
      std::cerr << get_name()
                << " gating: place_rate_bp=" << place_rate_bp
                << (place_rate_bp < 0 ? " (NEVER places passively)"
                                      : (place_rate_bp > 0 ? " (stochastic)"
                                                           : " (deterministic)"))
                << " place_after_n_eob=" << place_after_n_eob
                << " aggr_participation_bp=" << this->aggr_participation_bp
                << (this->aggr_participation_bp > 0 ? " (CROSSES)" : " (never crosses)")
                << std::endl;

      max_dist = this->pt.template get<int>("max_dist", 4);
      max_dist_cancel = this->pt.template get<int>("max_dist_cancel", max_dist + 2);
      ASSERTF(max_dist_cancel >= max_dist,
              boost::format("max_dist_cancel %d < max_dist %d: an order would be "
                            "cancelled at a distance it is still allowed to be "
                            "placed at, so placement would thrash")
                % max_dist_cancel % max_dist);
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
      if (m->timer_id == AGGR_TTL)
      {
        // Fill or be gone. If the cross is still live this many ms after it was
        // sent, it missed -- the touch moved between the decision and arrival --
        // and it is now an unintended passive order. Pull it.
        if (this->ord_info.has_value() && !this->ord_info.get_canc() &&
            this->ord_info.get_oid() == aggr_ttl_oid)
        {
          this->curr_tx_time = m->currtim._epoch_;   // same reason as below
          log_trd("CANCORD id: %d, aggressive order did not fill within %d ms",
                  this->ord_info.get_oid(), aggr_ttl_ms);
          this->cancel_order();
        }
        else if (this->ord_info.has_value())
        {
          log_inf("stale AGGR_TTL for oid %d, slot now holds %d -- not cancelling",
                  aggr_ttl_oid, this->ord_info.get_oid());
        }
        return true;
      }
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
          // stamps every Alarm with it (Timer.cpp:110), and the sim is one
          // ordered queue, so that stamp is never behind what we last saw.
          this->curr_tx_time = m->currtim._epoch_;
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
      log_inf("max_dist: %d, max_dist_cancel: %d, max working size: %d",
              max_dist, max_dist_cancel,
              std::min((max_dist + 1) * this->lev_orders_max.get(), this->all_orders_max.get()));
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

    // already_gated: the caller has already decided this event is one to act
    // on, so skip the placement-rate gate below. The aggressive path
    // (tradenotify_handler) flips its own coin against aggr_participation_bp;
    // running it through place_rate_bp as well would gate it twice and make
    // the realised aggressive share the PRODUCT of the two rates rather than
    // the one that was configured.
    void place_if_can_impl(payload_ptr_t payload, bool already_gated = false) noexcept
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
      // payload->sz applies to CROSSES TOO, deliberately.
      //
      // It was removed for the aggressive path once, to see what the cap was
      // worth: clip went 1.18 -> 7.2 lots and participation 0.77% -> 7.61%.
      // That is not a finding. Taking larger size raises participation by
      // definition, and without this term the light is no longer shadowing
      // anything -- it takes whatever size the fill model will grant, which
      // (fill_sim_on_arrival fills in full at the touch, never walking the
      // book, with no impact) is unlimited and free. The measurement would be
      // of the simulator, not of the market.
      //
      // Note lastQty on an MBO trade is the fill against ONE resting order,
      // not the aggressor's total, so this caps a cross at one counterparty's
      // slice. That is conservative, and conservative is the right direction
      // while impact is unmodelled.
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

      // An AGGRESSIVE placement must not attach. already_gated is set only by
      // the trade path, and everything a trade payload references is being
      // consumed at that instant -- so the anchor dies immediately and the
      // light cancels its own cross before it can fill. Measured 20250102
      // 09:30-10:30 at 800bp, delay 0: the two aggressive lights produced 293
      // CANCORDs, ALL of them "attached_order_id gone", against 61 for the
      // busiest of twelve passive lights.
      attached_order_id = payload->ex_order_id;
      attached_is_trade = already_gated;

      // Bank-wide dedup, and it applies to CROSSES TOO: ten aggressive lights
      // must not all cross on the same trade. This is why the id is now kept
      // for the aggressive path -- only the cancel use of it was wrong.
      // (Guarded on != 0 because 0 is QCoord's default: without that, an
      // unattached placement would match the default and suppress everything.)
      if (attached_order_id != 0 &&
          this->qcoord->get_attached_order_id() == attached_order_id)
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
      if (already_gated)
      {
        // nothing: the caller's own rate decided this one
      }
      else if (place_rate_bp < 0)
      {
        // NEVER place passively. This is what the aggressive bank needs: a
        // light that only ever crosses. 0 cannot mean this -- 0 selects the
        // deterministic branch below, and with place_after_n_eob 0 that places
        // on EVERY add, which is the opposite of the intent and is exactly the
        // bug the 2026-09-14 aggressive smoke hit (the "aggression only" light
        // turned out to be the busiest passive light in the run).
        return;
      }
      else if (place_rate_bp > 0)
      {
        // Precomputed threshold, NOT `rng() % 10000` -- 10000 is not a power of
        // two and this runs for every ADD. See light22_base::tradenotify_handler.
        if (uint64_t(rng()) >= place_rate_thresh)
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

        auto id = this->place_order(
            bestpx,
            possible_order_sz,
            this->stamp_of(payload));

        // place_order returns -1 if rate-limited by gunning protection
        if (id < 0)
          return;

        // Record the exchange order ID from the market data message that triggered this order placement
        this->qcoord->set_attached_order_id(attached_order_id);
        this->pending_cancel_eob = 0;

        // AGGRESSIVE: fill or be gone. An aggressive order has no anchor, so
        // nothing else would pull it if it misses -- it would rest until it
        // drifted max_dist_cancel ticks away or the leg completed, quietly
        // becoming a passive order the arm never asked for.
        if (already_gated && aggr_ttl_ms > 0)
        {
          aggr_ttl_oid = id;
          this->timer->send(new frame::mtim::msg::AlarmClockSub(
                                aggr_ttl_ms / 1000, aggr_ttl_ms % 1000,
                                this->AGGR_TTL, false),
                            this);
        }

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
      this->curr_tx_time = this->stamp_of(pld);
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
      // NOT for crosses. attached_is_trade says the id came from a trade, whose
      // referenced order is consumed at the instant we attach to it -- so this
      // branch would fire on the very next EOB and cancel the cross before it
      // can fill. A cross is pulled by aggr_ttl_ms, by the position target, or
      // by nothing at all; never by its anchor leaving, because it has none.
      if (this->ord_info.has_value() && !this->ord_info.get_canc() &&
          !this->attached_is_trade &&
          pld->ex_order_id == this->attached_order_id && this->attached_order_id != 0)
      {
        log_trd("CANCORD id: %d, attached_order_id %lu gone",
                this->ord_info.get_oid(), attached_order_id);
        this->pcoord->incr_attached_order_id_match();
        this->attached_order_id = 0;
        this->attached_is_trade = false;

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
