#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * SlippageProbe -- the actor that makes the simulator produce a number.
 *
 * Everything else in sim/ reconstructs a book and runs an algorithm against it.
 * Nothing asks the algorithm to do a job and measures what the job cost. This
 * does: it runs the shadow lights as a MARKET MAKER, quoting both sides at once
 * through the RTH session, and emits one CSV row per window.
 *
 * It is also why the simulator places orders at all. `targetpos` defaults to 0
 * and only Set(TARGET_POS) moves it, so with nothing driving the lights they
 * cancel on the "position is at target" branch forever. ORD: 0.
 *
 * ## The arrangement
 *
 *   first boundary in the session   Set(TARGET_POS, +sz) to every BUY light
 *                                   Set(TARGET_POS, -sz) to every SEL light
 *                                   -- the only target send there will ever be
 *
 *   each boundary after that        close the open window, emit its row,
 *                                   open the next
 *
 *   first boundary past 15:30       close the last window and stop measuring
 *
 * Nothing is sent at the end. TARGET_POS 0 is not an off switch: with a long
 * position, pos > targetpos, so the SELL side would become active and liquidate
 * the book. It is the flattening this design exists to remove, arriving through
 * the back door. Nothing needs standing down anyway -- the position
 * self-corrects inside the band and the replay stops at --end-ts.
 *
 * Both sides are live at the same time because their targets DIFFER: a BUY
 * light stands down at pos >= targetpos and a SEL light at pos <= targetpos, so
 * every position strictly inside (-sz, +sz) leaves both working. The inventory
 * random-walks inside that band and crosses zero on its own. Nothing flattens
 * it and nothing resets it -- a window's closing position is carried into the
 * next one and reported as pos_at_close.
 *
 * ## Each leg has its own window
 *
 * The timer supplies BOUNDARIES. At each one both legs open a window, so they
 * share a start and with it an anchor mid and anchor touch. They do not share
 * an end: a leg's window closes at the fill that completes its own size. One
 * side can be done in seconds while the other is still working, so durations and
 * market-volume denominators differ within a single row even though the anchors
 * match. A leg that never fills its size runs to the next boundary, and the
 * row's outcome names the side that fell short -- ok, buy_short, sel_short or
 * both_short.
 *
 * ## The numbers
 *
 *   slip_buy    = buy_vwap - mid_fire        buy side vs the anchor mid
 *   slip_sel    = mid_sell - sel_vwap        sell side vs the same anchor
 *   slip_paired = (buy_vwap - sel_vwap) / 2  the captured spread
 *   slip_legsum = (slip_buy + slip_sel) / 2
 *
 * slip_paired equals 1/2(cost_buy + cost_sel) only when both legs saw the same
 * mid -- which here they do, because they arrive together. That was the defect
 * in the earlier sequential design (buy the parent, then sell it back): the
 * legs were separated by however long the buy took, so market drift in between
 * was booked as execution cost, and at 100 lots the two columns disagreed 3x.
 *
 * Each side is also scored against the interval VWAP of its own leg, which
 * separates drift from selection, and against the touch it could have crossed
 * at, which says what patience bought. See emit_row().
 *
 * ## Time
 *
 * The Timer is the clock, via a periodic relative AlarmClockSub whose period IS
 * the window. The Timer runs on MARKET time -- it sets currtim from
 * EndOfBurst2's transactTime -- so the probe advances only when the session
 * does and a quiet book cannot age a window out.
 *
 * Deliberately NOT the absolute AlarmClockSub(h, m, s, ms, id) form:
 * chutil::Time formats with gmtime_r, so that form fires on a UTC hour and
 * would slip an hour against ET when DST starts on 2025-03-09, inside the test
 * window. The relative/periodic form has no timezone in it. The session bounds
 * (09:30 and 15:30 ET) are epoch ns computed by the caller against a real tz
 * database.
 *
 * EndOfBurst is subscribed only for the touch prices: the Timer gives the probe
 * time, the book gives it the mid.
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
#include "chutil/Assert.hpp"
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
      // The measured session, epoch ns. Outside it the lights are stood down
      // and nothing is recorded.
      uint64_t    session_start = 0;    // 09:30 ET
      uint64_t    session_end   = 0;    // 15:30 ET
      // The repeating timer's period IS the window. Every alarm closes one
      // window and opens the next -- there is no separate window length to keep
      // in step with it, and no schedule of fire times to walk.
      int         tick_s = 15 * 60;     // seconds of market time
      std::string out_path;             // CSV; empty = stderr summary only
    };

    SlippageProbe(actor_ptr ob, actor_ptr timer, const Config &cfg);
    ~SlippageProbe() override;

    // The lights are built after the SOM (which needs this actor as its fill
    // subscriber), so they are wired in afterwards rather than at construction.
    //
    // WHOLE SETS, not one light each. This used to take a single buy and a
    // single sell light, which was correct only while there was exactly one of
    // each. With N per side, set_lights(lights_[0], lights_[1]) handed over the
    // first TWO lights -- both of them BUY lights -- so no SEL light ever
    // received a target at all.
    //
    // That was not silent: SEL lights default to targetpos 0 and stand down
    // only at pos <= targetpos, so an untargeted SEL light is permanently armed
    // to flatten any long position. The sell "leg" was not being worked to a
    // target, it was six lights dumping inventory the instant it appeared --
    // which is why a 100-lot sell leg completed in 0.58s against a 71s buy leg.
    void set_lights(std::vector<actor_ptr> buys, std::vector<actor_ptr> sels)
    {
      buy_lights = std::move(buys);
      sel_lights = std::move(sels);
    }

    bool enabled() const { return cfg.parent_sz > 0 && cfg.session_end > cfg.session_start; }

  private:

    struct Leg
    {
      double   notional = 0;   // sum(px * sz), for the VWAP
      double   filled   = 0;
      int      n_fills  = 0;
      uint64_t started  = 0;
      uint64_t ended    = 0;
      // Market volume over THIS LEG'S OWN interval -- first fill to last fill,
      // not the clock window. The two legs quote simultaneously but they do not
      // trade simultaneously: one side can be done in seconds while the other is
      // still working, and charging both with the same denominator would flatter
      // the fast one and punish the slow one.
      //
      // Taken as a difference of session cumulatives snapshotted at this leg's
      // first and last fill, which is exact and costs nothing on the hot path.
      // A leg with no fills leaves these equal and reports zero.
      double   cum_vol_first = 0, cum_not_first = 0;
      double   cum_vol_last  = 0, cum_not_last  = 0;
      bool     anchored      = false;
      // A leg is FINISHED when it has filled its size -- its remaining size
      // went to 0. It stops accumulating there, so its interval is its own
      // first fill to the fill that completed it, not the clock window. The two
      // sides quote together but they do not finish together: one can be done
      // in seconds while the other is still working, and measuring both over the
      // window would flatter the fast one and punish the slow one.
      bool     done          = false;

      double mkt_vol()      const { return cum_vol_last - cum_vol_first; }
      double mkt_notional() const { return cum_not_last - cum_not_first; }

      // Snapshot the session cumulative as this leg's interval opens, then
      // extend it on every fill. Anchored at WINDOW OPEN, not at the first
      // fill, so the volume denominator spans exactly the interval the leg's
      // duration reports -- the leg is working from the moment the lights are
      // quoting, whether or not anything has hit it yet. Anchoring at the first
      // fill instead made participation divide by a shorter window than the
      // duration implied, inflating it.
      void mark(double cum_vol, double cum_not)
      {
        // Session cumulatives only ever grow, so a leg's interval can never
        // come out negative. If this trips, the leg was marked with a stale
        // snapshot and its participation and market VWAP are nonsense.
        ASSERT(cum_vol >= cum_vol_last, "market volume went backwards");
        if (!anchored) { anchored = true; cum_vol_first = cum_vol; cum_not_first = cum_not; }
        cum_vol_last = cum_vol;
        cum_not_last = cum_not;
      }

      double vwap() const { return filled > 0 ? notional / filled : 0.0; }
      // VWAP of the market over this leg's interval, in the same price units as
      // vwap(). 0 when nothing traded alongside us.
      double mkt_vwap() const { return mkt_vol() > 0 ? mkt_notional() / mkt_vol() : 0.0; }
      // Realised participation: our share of everything that traded while we
      // were working. This is the delivered quantity, NOT the order-placement
      // rate that produced it -- mapping one to the other is the point.
      double participation() const { return mkt_vol() > 0 ? filled / mkt_vol() : 0.0; }
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

    // The only place a target is ever sent. Called once, from start_handler.
    void send_targets() noexcept;
    void open_window(uint64_t now) noexcept;
    void close_window(uint64_t now) noexcept;
    void emit_header();
    void emit_row(const char *outcome);

    char       name[256];
    actor_ptr  ob        = nullptr;
    actor_ptr  timer     = nullptr;
    // Every light, split by side, so a target reaches all of them rather than
    // whichever one happened to be first in the vector. See set_lights().
    std::vector<actor_ptr> buy_lights, sel_lights;
    Config     cfg;
    // Is a window open right now? on_clock opens the first one and roll_window
    // asserts on it, so it is the one bit separating "first boundary of the
    // session" and "session over, last window already closed" from "a window is
    // open and this boundary closes it".
    bool     window_open = false;
    // Session-cumulative traded volume/notional. Each leg's own interval is a
    // difference of two snapshots of these. See Leg::mark().
    double   mkt_vol_cum = 0, mkt_not_cum = 0;
    size_t   n_windows = 0;      // windows opened so far
    int      position  = 0;      // our own, from fills

    uint64_t fire_ts   = 0;      // start of the open window
    int      mid_fire  = 0;      // ticks * 2, so a half-tick mid stays integral
    int      mid_sell  = 0;
    // The TOUCH at each leg's arrival, not just the mid. Three benchmarks
    // answer three different questions and only together say what happened:
    //
    //   vs mid at arrival    what the decision cost against the fair price
    //                        at the moment we committed. Contaminated by drift
    //                        over a long leg, which is why paired and legsum
    //                        diverge at 100 lots.
    //   vs interval VWAP     how we did against everyone trading alongside us.
    //                        Drift cancels, so what survives is selection.
    //   vs touch at arrival  what PATIENCE bought. We could have crossed the
    //                        spread at ask_fire and been done; resting instead
    //                        should beat it, and by how much is the whole case
    //                        for a passive algorithm. Expected NEGATIVE (a
    //                        saving) where the mid-based numbers are positive.
    int      ask_fire  = 0;      // the ask we could have lifted at t0
    int      bid_fire  = 0;
    int      bid_sell  = 0;      // the bid we could have hit at t1
    int      ask_sell  = 0;
    Leg      buy_leg;
    Leg      sel_leg;

    int  last_bid = 0;
    int  last_ask = 0;
    uint64_t last_tim = 0;

    // Windows where both legs filled their size, and where one did not.
    int  fires_done = 0, fires_partial = 0;
    uint64_t n_eob = 0, n_ticks = 0;
    FILE *out = nullptr;
  };

}  // namespace sim
