#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * SlippageProbe — the thing that makes the simulator produce a number.
 *
 * Everything else in sim/ reconstructs a book and runs an algorithm against it.
 * Nothing asks the algorithm to do a job and measures what the job cost. This
 * does: every 30 minutes from 09:30 to 15:00 ET it hands the shadow lights a
 * parent order, watches the fills come back, and records the execution price
 * against the mid at the moment the order was released.
 *
 * It is also why the simulator has never placed an order. `targetpos` defaults
 * to 0 and only Set(TARGET_POS) moves it, so with nothing driving the lights
 * they cancel on the "position is at target" branch forever. ORD: 0.
 *
 * ## One fire
 *
 *   t0  record mid from the current book         -> mid_fire
 *       Set(TARGET_POS, +n) on both lights       -> the BUY light works
 *   ..  fills arrive; position climbs to +n
 *   t1  record mid                               -> mid_sell
 *       Set(TARGET_POS, 0)                       -> the SEL light works
 *   ..  fills arrive; position returns to 0
 *   t2  emit a row
 *
 * Both lights get the same target because that is what selects between them:
 * the BUY light stands down while pos >= targetpos and the SEL light while
 * pos <= targetpos, so one value drives the pair with no extra coordination.
 *
 * ## The number
 *
 *   slip_buy    = buy_vwap - mid_fire        cost of the buy leg, ticks
 *   slip_sel    = mid_sell - sel_vwap        cost of the sell leg, ticks
 *   slip_paired = (buy_vwap - sel_vwap) / 2
 *
 * The paired form is direction-agnostic: written out, it is the average of the
 * two leg costs with the reference mid cancelling algebraically -- but only if
 * both legs saw the SAME mid, which sequential legs do not. So both forms are
 * emitted. Per-leg is the defensible measurement; paired is the one that needs
 * no reference price, and comparing them says how much the drift between legs
 * matters. If they disagree materially, the parent is too big or the session
 * too thin for the window, and that is a result worth having rather than an
 * average worth hiding.
 *
 * ## Time
 *
 * The Timer is the clock, via a periodic relative AlarmClockSub. The Timer runs
 * on MARKET time -- it sets currtim from EndOfBurst2's transactTime and fires
 * alarms against that -- so the probe advances only when the session does and a
 * quiet book cannot age a leg out.
 *
 * The schedule itself is a list of epoch ns computed by the caller against a
 * real tz database, and each tick is compared against it. What it deliberately
 * is NOT is the absolute AlarmClockSub(h, m, s, ms, id) form: chutil::Time
 * formats with gmtime_r, so that form fires on a UTC hour and would slip an
 * hour against ET when DST starts on 2025-03-09 -- inside the test window. The
 * relative/periodic form has no timezone in it, so the standard mechanism and
 * a correct schedule are not in tension.
 *
 * EndOfBurst is still subscribed, but only for the touch prices: the Timer
 * gives the probe time, the book gives it the mid.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "enum/e_names.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "frame/som/msg/Fill.hpp"
#include "light/msg/Set.hpp"
#include "logger/act/Logger.hpp"

namespace sim
{

  struct SlippageProbe : public actors::Actor
  {
    const char *get_name() const override { return name; }

    // ---- configuration -------------------------------------------------
    struct Config
    {
      std::string sym_name;             // instrument being probed, for the CSV
      uint32_t    sym = 0;              // asset id, to filter fills
      int         parent_sz = 0;        // contracts per leg; 0 disables the probe
      std::vector<uint64_t> fire_ts;    // epoch ns, ascending
      uint64_t    leg_timeout_ns = 600ull * 1000000000ull;  // 10 min per leg
      int         tick_s = 1;           // clock granularity, seconds of market time
      std::string out_path;             // CSV; empty = stderr summary only
    };

    SlippageProbe(actor_ptr ob, actor_ptr timer, const Config &cfg);
    ~SlippageProbe() override;

    // The lights are built after the SOM (which needs this actor as its fill
    // subscriber), so they are wired in afterwards rather than at construction.
    void set_lights(actor_ptr buy, actor_ptr sel)
    {
      light_buy = buy;
      light_sel = sel;
    }

    bool enabled() const { return cfg.parent_sz > 0 && !cfg.fire_ts.empty(); }

  private:
    enum class Phase { WAITING, BUYING, SELLING };

    struct Leg
    {
      double   notional = 0;   // sum(px * sz), for the VWAP
      double   filled   = 0;
      int      n_fills  = 0;
      uint64_t started  = 0;
      uint64_t ended    = 0;
      // TOTAL market volume that traded while this leg was working -- not
      // buy-side volume. Every trade has a buyer and a seller, so a buy/sell
      // split of the same interval would be identical; what differs between
      // the legs is the INTERVAL, since the buy leg and the sell leg run at
      // different times and for different durations. The denominator of
      // realised participation.
      double   mkt_vol  = 0;

      double vwap() const { return filled > 0 ? notional / filled : 0.0; }
      // Realised participation: our share of everything that traded while we
      // were working. This is the delivered quantity, NOT the order-placement
      // rate that produced it -- mapping one to the other is the point.
      double participation() const { return mkt_vol > 0 ? filled / mkt_vol : 0.0; }
      void   reset() { *this = Leg{}; }
    };

    enum TimerIds { PROBE_TICK = 700 };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void alarm_handler(const frame::mtim::msg::Alarm *m) noexcept;
    void eob_handler(const frame::ob::msg::EndOfBurst *m) noexcept;
    void trade_handler(const frame::ob::msg::TradeNotify *m) noexcept;
    void fill_handler(const frame::som::msg::Fill *m) noexcept;
    void on_clock(uint64_t now) noexcept;

    void set_target(int n) noexcept;
    void begin_fire(uint64_t now) noexcept;
    void begin_sell_leg(uint64_t now) noexcept;
    void finish_fire(uint64_t now, const char *why) noexcept;
    void emit_header();
    void emit_row(const char *outcome);

    char       name[256];
    actor_ptr  ob        = nullptr;
    actor_ptr  timer     = nullptr;
    actor_ptr  light_buy = nullptr;
    actor_ptr  light_sel = nullptr;
    Config     cfg;

    Phase    phase     = Phase::WAITING;
    size_t   next_fire = 0;      // index into cfg.fire_ts
    int      position  = 0;      // our own, from fills

    uint64_t fire_ts   = 0;      // the fire in flight
    int      mid_fire  = 0;      // ticks * 2, so a half-tick mid stays integral
    int      mid_sell  = 0;
    Leg      buy_leg;
    Leg      sel_leg;

    int  last_bid = 0;
    int  last_ask = 0;
    uint64_t last_tim = 0;

    int  fires_done = 0, fires_partial = 0, fires_skipped = 0;
    uint64_t n_eob = 0, n_ticks = 0;
    FILE *out = nullptr;
  };

}  // namespace sim
