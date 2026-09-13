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
 * It is also why the simulator places orders at all. A light with `targetpos`
 * 0 and a flat book has nothing to do -- it cancels on the "position is at
 * target" branch forever -- so with nothing driving it the simulator places no
 * orders at all. ORD: 0.
 *
 * ## The arrangement
 *
 * NO TARGET IS EVER SENT. This is the PositionManager arrangement
 * (PositionManager.hpp:56): `targetpos` stays 0 on every light and the POSITION
 * is what gives a light work. From light22.hpp:239,243, with targetpos 0:
 *
 *     BUY works only while pos < 0       (short -> buy it back)
 *     SEL works only while pos > 0       (long  -> sell it down)
 *     pos == 0                            both idle
 *
 * So a leg is started by telling its book it holds the OPPOSITE position, and
 * it finishes when the lights have worked that book back to flat. The position
 * returns to 0 ON ITS OWN; nothing flattens it and nothing resets it.
 *
 * Because no single position value leaves both sides live, each side gets its
 * OWN PCoord (SimKaspr::create_lights). That is what lets the two legs run over
 * the same window instead of one after the other:
 *
 *   every window opens              buy book += SEL sz   (pos -sz, BUY works)
 *                                   sel book += BUY sz   (pos +sz, SEL works)
 *
 *   the window closes when          the minimum has elapsed AND both legs have
 *                                   filled their size
 *
 *   first boundary past 16:00       stop opening windows; the lights keep
 *                                   whatever work they still hold
 *
 * The work handed out grows by one parent per side per window and is never
 * taken back. Each book returns to 0 by itself inside its window, so the two
 * stay in step and neither side ever runs out of room to quote -- the failure
 * of a fixed band, where one side buys its size once and then stands down for
 * the rest of the session.
 *
 * Nothing is sent at the end of the session either. There is no stand-down to
 * send: a book that is already flat is idle, and a book that is not is still
 * being worked, which is the honest state to leave it in.
 *
 * ## When a window ends -- the requirement, stated plainly
 *
 * A window ends when BOTH of these hold:
 *
 *   1. its MINIMUM duration has elapsed (Config::min_window_ns), and
 *   2. BOTH legs have filled their size.
 *
 * Not either alone. The minimum is a FLOOR, not a deadline.
 *
 *   period 5 min, buy fills in 10s, sell in 1 min:
 *
 *     t=0      window opens, both sides quote
 *     t=10s    buy leg done   -> buy measured over 0..10s
 *     t=1m     sell leg done  -> sell measured over 0..1m
 *     t=1m..5m nothing measured; the lights keep quoting
 *     t=5m     minimum elapsed and both done -> emit, open the next window
 *
 *   same period, sell leg still at 60/100 at 5 min:
 *
 *     t=5m     minimum elapsed but a leg is short -> KEEP WAITING
 *     t=8m     sell leg fills -> emit, open the next window
 *
 * So a thin session yields fewer, longer windows rather than short rows. There
 * is no partial row in the corpus and no abort.
 *
 * Why not close on the clock alone, as a fixed period? Because a leg that had
 * not filled would be written out as if it had, and the VWAPs and
 * participations in that row would not mean what their column names say.
 *
 * Why not assert instead? Because that turns a thin session into a dead run,
 * and an aborted run is worse than a missing one: it leaves a truncated CSV
 * that `run_grid.sh`'s resume guard (more than one line) and `aggregate_grid.py`
 * both accept as complete. The sessions that would abort are the hostile ones,
 * so asserting biases the corpus toward benign windows.
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
 *   slip_sel    = mid_fire - sel_vwap        sell side vs the same anchor
 *   slip_paired = (buy_vwap - sel_vwap) / 2  the captured spread
 *   buy_drift   = buy_end_mid - mid_fire     where the mid went while the buy
 *                                            leg worked (0 if it never finished)
 *   sel_drift   = sel_end_mid - mid_fire     likewise for the sell leg
 *   drift       = mid_close  - mid_fire      over the whole window
 *
 * There is no slip_legsum. It was (slip_buy + slip_sel)/2, which is worth
 * having only while the two legs have SEPARATE arrivals to difference. They
 * arrive together here, so it reduces exactly to slip_paired -- two columns
 * carrying one number. Drift is measured forward instead: from the common
 * arrival to where each leg finished, and to where the window closed.
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
 * (09:29 and 16:00 ET) are epoch ns computed by the caller against a real tz
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
#include "light/qcoord.hpp"
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
      uint64_t    session_start = 0;    // 09:29 ET
      uint64_t    session_end   = 0;    // 16:00 ET
      // How often the probe looks at the clock. This is a POLL, not the window:
      // a window ends when its minimum has elapsed AND both legs have filled
      // their size, so the probe has to check more often than the minimum.
      int         tick_s = 1;           // seconds of market time

      // The minimum a window runs for. It is a floor, not a deadline: if a leg
      // has not filled its size when the minimum elapses, the window stays open
      // until it does. A thin session therefore produces fewer, longer windows
      // rather than short rows -- there is no partial row and no abort.
      // 10 minutes. The legs themselves finish in seconds -- 6.5s mean on a
      // measured session -- so this is not sized to give them time to fill. It
      // is sized to give each side room to come back to flat AND to sit square
      // for a while before the next parent lands on it, which is what keeps an
      // overfill from running into the following window's measurement.
      uint64_t    min_window_ns = 10ull * 60ull * 1000000000ull;
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

    // The two position books, one per side. THIS is the control surface: no
    // target is ever sent. A light with targetpos 0 works only while the
    // position is on the other side of flat (light22.hpp:239,243), so a leg is
    // started by telling its book it holds the OPPOSITE position and is finished
    // when the lights have worked that book back to 0 -- exactly what
    // PositionManager does (PositionManager.hpp:56).
    void set_coords(light::PCoord *buys, light::PCoord *sels)
    {
      pcoord_buy = buys;
      pcoord_sel = sels;
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
      // What this side actually has to execute THIS window: one parent, plus
      // or minus whatever the last window over- or under-shot. Set at window
      // open from the probe's own running totals, so completion never depends
      // on reading a position book that another actor updates.
      int      to_do    = 0;
      // 2x the mid at the fill that COMPLETED this leg, for its own drift.
      // Zero while the leg is unfinished, which is how emit_row knows not to
      // report a drift to a mid the leg never reached.
      int      end_mid  = 0;
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
      // The same difference taken over this leg's OWN AGGRESSOR STREAM: hits
      // for the buy leg, takes for the sell leg. See Config's note on the
      // hit/take benchmark for why that pairing and not the other.
      double   cum_agg_vol_first = 0, cum_agg_not_first = 0;
      double   cum_agg_vol_last  = 0, cum_agg_not_last  = 0;
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
      double agg_vol()      const { return cum_agg_vol_last - cum_agg_vol_first; }
      double agg_notional() const { return cum_agg_not_last - cum_agg_not_first; }

      // Snapshot the session cumulative as this leg's interval opens, then
      // extend it on every fill. Anchored at WINDOW OPEN, not at the first
      // fill, so the volume denominator spans exactly the interval the leg's
      // duration reports -- the leg is working from the moment the lights are
      // quoting, whether or not anything has hit it yet. Anchoring at the first
      // fill instead made participation divide by a shorter window than the
      // duration implied, inflating it.
      void mark(double cum_vol, double cum_not,
                double cum_agg_vol, double cum_agg_not)
      {
        // Session cumulatives only ever grow, so a leg's interval can never
        // come out negative. If this trips, the leg was marked with a stale
        // snapshot and its participation and market VWAP are nonsense.
        ASSERT(cum_vol >= cum_vol_last, "market volume went backwards");
        if (!anchored)
        {
          anchored = true;
          cum_vol_first     = cum_vol;     cum_not_first     = cum_not;
          cum_agg_vol_first = cum_agg_vol; cum_agg_not_first = cum_agg_not;
        }
        cum_vol_last     = cum_vol;     cum_not_last     = cum_not;
        cum_agg_vol_last = cum_agg_vol; cum_agg_not_last = cum_agg_not;
      }

      double vwap() const { return filled > 0 ? notional / filled : 0.0; }
      // VWAP of the market over this leg's interval, in the same price units as
      // vwap(). 0 when nothing traded alongside us.
      double mkt_vwap() const { return mkt_vol() > 0 ? mkt_notional() / mkt_vol() : 0.0; }
      // VWAP of the trades this leg was COMPETING WITH for fills: the hits, if
      // this is the buy leg; the takes, if it is the sell leg. 0 when no such
      // trade landed beside the leg, which the aggregator filters.
      double agg_vwap() const { return agg_vol() > 0 ? agg_notional() / agg_vol() : 0.0; }
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

    // Hand both sides one parent of work, at the open of every window. THE
    // ONLY PLACE the probe touches a position.
    void give_work() noexcept;
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
    // Is a window open right now? on_clock opens the first one and closes
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
    light::PCoord *pcoord_buy = nullptr;
    light::PCoord *pcoord_sel = nullptr;

    // Total handed to each side so far. Normally one parent per window each,
    // but the two diverge when a side runs ahead of its work and has to be
    // given more than one to put it back to work -- so they are counted
    // separately rather than sharing a total.
    int      work_given_buy = 0;
    int      work_given_sel = 0;

    // Everything each side has filled since the session began. work_given minus
    // one of these is that side's outstanding work, which is what a leg has to
    // execute to be finished -- computed from the probe's OWN counters rather
    // than from the PCoord, because the book is updated by the light's fill
    // handler and this actor's fill handler is a separate dispatch: reading it
    // here would be a race on who was served first.
    double   buy_filled_total = 0;
    double   sel_filled_total = 0;

    // Session cumulatives split by AGGRESSOR, from data_pay_load::is_hit() and
    // is_tak() (Data.hpp). `side` on a trade is the RESTING order's side, so:
    //
    //   is_hit()  resting BUY  -- a bid was hit   -- the aggressor SOLD
    //   is_tak()  resting SEL  -- an offer taken  -- the aggressor BOUGHT
    //
    // Our buy leg rests on the bid and is filled by someone hitting it, so the
    // hits are the trades it was competing with: other passive bids that got
    // filled over the same interval. That is the peer group, and comparing
    // against it answers a different question from mkt_vwap -- not "did we beat
    // the average trade" but "did we beat the other resting bids".
    double   mkt_vol_hit = 0, mkt_not_hit = 0;
    double   mkt_vol_tak = 0, mkt_not_tak = 0;

    // How far past the minimum the open window has run, at which the next
    // stall warning is due. Reset on every open; grows by one minimum per
    // warning so a permanently stalled leg complains periodically, not per tick.
    uint64_t stall_warn_at = 0;

    int      mid_fire  = 0;      // ticks * 2, so a half-tick mid stays integral
    // 2x the mid the window CLOSED on. Both legs arrive together, so drift
    // cannot be the gap between two arrivals any more -- it is measured forward
    // from mid_fire to here, and per leg to Leg::end_mid.
    int      mid_close = 0;
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
    int      ask_fire  = 0;      // the ask we could have lifted at the arrival
    int      bid_fire  = 0;      // the bid we could have hit at the same instant
    // There is no ask_sell / bid_sell pair any more: with both legs arriving at
    // the same instant they were copies of these two, assigned every window and
    // read nowhere.
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
