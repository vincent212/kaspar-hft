/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * SlippageProbe -- the actor that makes the simulator produce a number.
 *
 * ## What it does
 *
 * It runs the shadow lights as a MARKET MAKER and measures what the two sides
 * cost. Both sides quote at the same time, continuously, for the RTH session;
 * the probe slices that session into fixed windows and emits one CSV row per
 * window.
 *
 * ## How, in four facts
 *
 * 1. **It sets a target once and never again.** BUY lights are told
 *    `target_pos = +sz`, SEL lights `-sz`. That is the entire control surface.
 *    A BUY light stands down at `pos >= targetpos` and a SEL light at
 *    `pos <= targetpos` (light22.hpp), so every position strictly inside
 *    `(-sz, +sz)` leaves BOTH sides live. They quote against each other, the
 *    inventory random-walks inside the band and crosses zero on its own.
 *    Nothing flattens it, nothing resets it, and no second target is ever sent
 *    -- not even at the end of the session. TARGET_POS 0 does not stand a light
 *    down: it points the SELL side at flat and makes it liquidate the book.
 *
 * 2. **The repeating timer is the whole clock.** One periodic AlarmClockSub,
 *    period = the window. Every alarm is a boundary: it closes the open window,
 *    emits its row, and opens the next. There is no schedule to walk, no window
 *    length held separately from the timer, and no phase machine.
 *
 * 3. **Each leg has its OWN window.** The two share a start -- the timer
 *    boundary, and with it the anchor mid and touch -- but not an end: a leg's
 *    window closes at the fill that completes its own size. One side can be done
 *    in seconds while the other is still working, so the durations and the
 *    market-volume denominators in a row differ even though the anchors match.
 *    A leg that never fills its size runs to the next boundary, and the row's
 *    outcome names the side that fell short.
 *
 * 4. **Inventory is carried, never unwound.** Whatever position a window ends on
 *    stays on the book; the lights carry it into the next window and their own
 *    targets pull it back toward the band. `pos_at_close` reports it.
 *
 * ## Why it is shaped this way
 *
 * The earlier design fired a SEQUENTIAL round trip: buy the parent, then sell it
 * back. Three things were wrong with it, and each is why a piece of the above
 * exists:
 *
 * - The legs ran at different times, so whatever the market did in between
 *   landed in `slip_paired` as if it were execution cost. At 100 lots the gap
 *   was ~170s and the two slippage columns disagreed by 3x.
 * - After each fire it set both targets to 0 and let the lights TRADE back to
 *   flat. That unwind was about as much volume as the measurement itself, at
 *   real prices, executed after the row was written -- invisible in it.
 * - `set_target` was called from six places, so the target was set ad hoc from
 *   scattered branches rather than being a property of the session.
 *
 * ## The numbers
 *
 *   slip_buy    = buy_vwap - mid_fire        buy side vs the anchor mid
 *   slip_sel    = mid_sell - sel_vwap        sell side vs the same anchor
 *   slip_paired = (buy_vwap - sel_vwap) / 2  the captured spread
 *   slip_legsum = (slip_buy + slip_sel) / 2
 *
 * plus each side against the interval VWAP of its own window (which separates
 * drift from selection) and against the touch it could have crossed at (which
 * says what patience bought). See emit_row().
 */

#include "sim/act/SlippageProbe.hpp"

#include <cinttypes>
#include <cstring>

namespace sim
{

SlippageProbe::SlippageProbe(actor_ptr _ob, actor_ptr _timer, const Config &_cfg)
    : ob(_ob), timer(_timer), cfg(_cfg)
{
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(frame::mtim::msg::Alarm, alarm_handler);
  MESSAGE_HANDLER(frame::ob::msg::EndOfBurst, eob_handler);
  MESSAGE_HANDLER(frame::ob::msg::TradeNotify, trade_handler);
  MESSAGE_HANDLER(frame::som::msg::Fill, fill_handler);
  snprintf(name, sizeof(name), "SlipProbe_%s", cfg.sym_name.c_str());
}

SlippageProbe::~SlippageProbe()
{
  if (out) fclose(out);
}

void SlippageProbe::start_handler(const actors::msg::Start *) noexcept
{
  if (!enabled())
  {
    log_inf("probe disabled (parent_sz=%d session=%llu..%llu)", cfg.parent_sz,
            (unsigned long long)cfg.session_start, (unsigned long long)cfg.session_end);
    return;
  }

  // The Timer is the clock. A relative periodic alarm, not the absolute
  // AlarmClockSub(h,m,s,ms,id) form -- that one is UTC (chutil::Time formats
  // with gmtime_r) and would slip an hour against ET at the DST change inside
  // the test window. Relative has no timezone in it, and the Timer runs on
  // market time, so a tick is a tick of the session.
  ASSERT(timer, "probe has no timer");
  ASSERT(ob, "probe has no book");
  ASSERTF(cfg.parent_sz > 0, boost::format("parent_sz %d must be > 0") % cfg.parent_sz);
  ASSERTF(cfg.tick_s > 0, boost::format("tick_s %d must be > 0") % cfg.tick_s);
  ASSERTF(cfg.session_end > cfg.session_start,
          boost::format("session ends before it starts: %llu .. %llu")
            % (unsigned long long)cfg.session_start
            % (unsigned long long)cfg.session_end);
  timer->send(new frame::mtim::msg::AlarmClockSub(cfg.tick_s, 0, PROBE_TICK, true), this);

  // The book, only for the touch prices.
  ob->send(new frame::mda::msg::Subscribe(frame::mda::msg::Subscribe::HI), this);

  if (!cfg.out_path.empty())
  {
    out = fopen(cfg.out_path.c_str(), "w");
    if (!out)
      log_err("cannot open %s for writing, falling back to log only", cfg.out_path.c_str());
    else
      emit_header();
  }

  fprintf(stderr, "probe: armed on %s, window=%ds, session=%llu..%llu\n",
          cfg.sym_name.c_str(), cfg.tick_s,
          (unsigned long long)cfg.session_start,
          (unsigned long long)cfg.session_end);
  log_inf("probe armed: sym=%s parent_sz=%d window=%ds -- targets are NOT sent "
          "here, the first alarm inside the session sends them",
          cfg.sym_name.c_str(), cfg.parent_sz, cfg.tick_s);
}

void SlippageProbe::send_targets() noexcept
{
  // THE ONLY PLACE A TARGET IS EVER SENT, and it is reached from exactly one
  // line in roll_window().
  //
  // BUY lights get +sz, SEL lights get -sz. That is the entire control surface.
  // A BUY light stands down at pos >= targetpos and a SEL light at
  // pos <= targetpos (light22.hpp:239,243), so every position strictly inside
  // (-sz, +sz) leaves BOTH sides live. They quote against each other, the
  // inventory random-walks inside the band and crosses zero on its own, and the
  // pair keeps buying and selling for as long as the session runs.
  //
  // Sent once and never revised. In particular NOT reset to 0 at the end of the
  // session: targetpos 0 does not stand a light down, it points the sell side at
  // flat and makes it liquidate whatever is on the book.
  // Both sides must actually be wired up. This is the check that was missing
  // when set_lights() took two pointers instead of two vectors: with N lights a
  // side it was handed lights_[0] and lights_[1] -- both BUY lights -- so no SEL
  // light ever received a target, and an untargeted SEL light is not idle, it is
  // permanently armed to flatten any long. Nothing failed; the numbers were just
  // wrong, for an entire corpus.
  ASSERT(!buy_lights.empty(), "no BUY lights: nothing would ever bid");
  ASSERT(!sel_lights.empty(), "no SEL lights: nothing would ever offer");

  const int sz = cfg.parent_sz;
  for (auto &l : buy_lights) if (l) l->send(new light::msg::Set(light::msg::Set::TARGET_POS,  sz), this);
  for (auto &l : sel_lights) if (l) l->send(new light::msg::Set(light::msg::Set::TARGET_POS, -sz), this);

  log_opr("targets %+d / %+d across %zu+%zu lights",
          sz, -sz, buy_lights.size(), sel_lights.size());
}

void SlippageProbe::open_window(uint64_t now) noexcept
{
  // EACH LEG HAS ITS OWN WINDOW. They share a start -- this boundary -- and so
  // share an anchor mid and touch, but they do not share an end: a leg's window
  // closes at the fill that completes its own size. One side can be done in
  // seconds while the other is still working, which is why the durations and the
  // market-volume denominators in a row differ even though the anchors match.
  // Every price in the row is measured against this touch, so a crossed or
  // one-sided book here would silently poison the whole window.
  ASSERTF(last_bid > 0 && last_ask > 0 && last_bid < last_ask,
          boost::format("window %zu opening on a bad touch: bid %d ask %d")
            % (n_windows + 1) % last_bid % last_ask);

  fire_ts  = now;
  mid_fire = last_bid + last_ask;     // 2x the mid, kept integral
  ask_fire = last_ask;                // what crossing would have cost the buy
  bid_fire = last_bid;
  mid_sell = mid_fire;                // same arrival for the sell side
  bid_sell = last_bid;                // what crossing would have got the sell
  ask_sell = last_ask;

  buy_leg.reset();
  sel_leg.reset();
  buy_leg.started = sel_leg.started = now;
  // Anchor both volume denominators HERE, at window open, so each leg's
  // market-volume interval matches the duration it reports. Anchoring on the
  // first fill instead made participation divide by a shorter span than
  // buy_ns/sel_ns described.
  buy_leg.mark(mkt_vol_cum, mkt_not_cum);
  sel_leg.mark(mkt_vol_cum, mkt_not_cum);

  window_open = true;
  ++n_windows;

  log_inf("WINDOW %zu at tx=%llu mid=%.1f pos=%d",
          n_windows, (unsigned long long)now, mid_fire / 2.0, position);
}

void SlippageProbe::close_window(uint64_t now) noexcept
{
  // Only ever called with a window open -- on_clock decides that.
  ASSERT(window_open, "close_window with no window open");

  // BOTH legs must have filled their size by the time the window closes. Each
  // one keeps the stamp of the fill that completed it -- its window is its own,
  // ending before this boundary, not at it.
  //
  // A leg that did not get there means the window is too short, or the size too
  // large, for the liquidity in it: the row would then be reporting a partial
  // execution as if it were a completed one, and the VWAPs and participations
  // in it would not mean what the column names say. Stop instead, with the
  // state intact, because the alternative is a plausible wrong number.
  ASSERTF(buy_leg.done && sel_leg.done,
          boost::format("window %zu closed with an unfinished leg: buy %.0f/%d%s, "
                        "sel %.0f/%d%s -- the window is too short or the size too "
                        "large for this book")
            % n_windows % buy_leg.filled % cfg.parent_sz % (buy_leg.done ? "" : " SHORT")
            % sel_leg.filled % cfg.parent_sz % (sel_leg.done ? "" : " SHORT"));

  // Time must run forward and the window must not be empty. A boundary at or
  // before the window's own start means the Timer handed us a stale or repeated
  // clock, which would emit a row covering no time at all.
  ASSERTF(now > fire_ts,
          boost::format("window %zu closing at %llu, at or before its start %llu")
            % n_windows % (unsigned long long)now % (unsigned long long)fire_ts);

  // A leg's end stamp is the fill that completed it, so it must fall inside the
  // window. Outside means the leg was stamped from the wrong clock -- the bug
  // that quantised every leg to the 1s tick before the stamp was taken from the
  // fill itself.
  ASSERTF(buy_leg.ended >= buy_leg.started && buy_leg.ended <= now,
          boost::format("buy leg ended at %llu, outside its window %llu..%llu")
            % (unsigned long long)buy_leg.ended
            % (unsigned long long)buy_leg.started % (unsigned long long)now);
  ASSERTF(sel_leg.ended >= sel_leg.started && sel_leg.ended <= now,
          boost::format("sel leg ended at %llu, outside its window %llu..%llu")
            % (unsigned long long)sel_leg.ended
            % (unsigned long long)sel_leg.started % (unsigned long long)now);

  // Both legs opened at this window's start -- they share an arrival even though
  // they do not share an end.
  ASSERT(buy_leg.started == fire_ts && sel_leg.started == fire_ts,
         "a leg did not start at the window it belongs to");

  // Every fill must have a price, or the VWAP is a division by a size that was
  // never paid for.
  ASSERT(buy_leg.notional > 0 && sel_leg.notional > 0,
         "a leg filled its size with no notional");

  // The inventory this window ends on is REAL and stays on the book. It is not
  // unwound and not reset: the lights carry it into the next window and their
  // own targets pull it back toward the band. pos_at_close records it, because a
  // window that ended at the edge of the band was run over on one side, and that
  // is a result rather than an error.
  emit_row("ok");
  ++fires_done;
  log_inf("window %zu closed: bought %.0f sold %.0f pos=%d",
          n_windows, buy_leg.filled, sel_leg.filled, position);

  window_open = false;
}

void SlippageProbe::eob_handler(const frame::ob::msg::EndOfBurst *m) noexcept
{
  if (!enabled()) return;
  auto pld = m->payload;
  if (uint32_t(pld->sym) != cfg.sym) return;

  ++n_eob;
  const int bid = pld->point_.bid_px[0];
  const int ask = pld->point_.ask_px[0];
  if (bid > 0 && ask > 0 && bid < ask)   // ignore an empty or locked touch
  {
    last_bid = bid;
    last_ask = ask;
  }
}

void SlippageProbe::alarm_handler(const frame::mtim::msg::Alarm *m) noexcept
{
  if (!enabled() || m->timer_id != PROBE_TICK) return;
  ++n_ticks;
  // Alarm::currtim is the Timer's market clock, built with from_epoch, so
  // _epoch_ is the transactTime in ns.
  const uint64_t now = m->currtim._epoch_;
  if (now == 0) return;
  last_tim = now;
  on_clock(now);
}

void SlippageProbe::on_clock(uint64_t now) noexcept
{
  // Every alarm of the repeating timer is a window boundary, and a boundary does
  // two things at most: close what is open, and open the next one if we are
  // still inside the session.
  if (now < cfg.session_start) return;          // before the session we measure
  if (last_bid <= 0 || last_ask <= 0) return;   // need a touch to anchor the row

  const bool in_session = now < cfg.session_end;

  if (window_open)
    close_window(now);
  else if (!in_session)
    return;                 // session over and the last window already closed
  else
    // THE ONE CALL SITE for a target, on the first boundary of the session.
    // There is deliberately no matching send at the end: TARGET_POS 0 does not
    // stand a light down, it points the SELL side at flat and makes it liquidate
    // the book -- the flattening this design removed, arriving through the back
    // door. Nothing needs standing down: the position self-corrects inside the
    // band and the replay stops at --end-ts.
    send_targets();

  if (in_session)
    open_window(now);
}

void SlippageProbe::trade_handler(const frame::ob::msg::TradeNotify *m) noexcept
{
  if (!enabled() || !m->payload) return;
  if (uint32_t(m->payload->sym) != cfg.sym) return;

  // Accumulate the SESSION cumulative. Each leg takes a difference of it over
  // its OWN window (Leg::mark) -- the two legs share a start but not an end, so
  // one side can be done in seconds while the other is still working, and a
  // shared denominator would flatter whichever finished first.
  //
  // The price is on the payload and used to be thrown away here, which left the
  // interval VWAP -- the benchmark that separates drift from selection --
  // unobtainable from the CSV.
  mkt_vol_cum += double(m->payload->sz);
  mkt_not_cum += double(m->payload->sz) * double(m->payload->px.to_int());
}

void SlippageProbe::fill_handler(const frame::som::msg::Fill *m) noexcept
{
  if (!enabled()) return;
  if (m->sym != cfg.sym) return;

  const double px = m->pxi;
  const double sz = m->sz;
  ASSERTF(sz > 0, boost::format("fill with size %.0f") % sz);
  ASSERTF(px > 0, boost::format("fill at price %.0f") % px);

  if (m->side == en::bs::BUY) position += int(sz);
  else                        position -= int(sz);

  // Attribute by the SET that produced the fill, not by side.
  //
  // Side identifies the set only while each is still moving toward its target
  // from flat. When a set unwinds, its side inverts -- set A is long and sells,
  // set B is short and buys -- so routing by side sends every liquidation fill
  // to the wrong leg. Set A trades as SIMULATOR, set B as SIMULATOR2.
  //
  // Side identifies the light exactly: a light22<BUY> only ever bids and a
  // light22<SEL> only ever offers. Nothing unwinds a position here, so there is
  // no case where a light trades against its own side and the attribution
  // inverts.
  Leg &leg = (m->side == en::bs::BUY) ? buy_leg : sel_leg;

  // A finished leg is closed for measurement. Both sides keep quoting for the
  // rest of the window -- the position oscillates inside the band and fills keep
  // arriving -- but this window's measurement for THIS side is the first `sz`
  // lots it got, timed from its first fill to the one that completed it.
  if (leg.done) return;
  leg.notional += px * sz;
  leg.filled   += sz;
  ++leg.n_fills;
  // Opens this leg's interval on its first fill and extends it on every one
  // after, so its market-volume denominator spans exactly the time it was
  // actually trading.
  leg.mark(mkt_vol_cum, mkt_not_cum);

  // Finished the moment the remaining size hits 0. Its end stamp is this fill,
  // not the clock tick that later notices, and not the window boundary.
  // A leg overshoots by at most one clip: the lights stand down the moment the
  // position reaches the target, so anything beyond that is an attribution error
  // -- fills from the wrong side landing in this leg.
  ASSERTF(leg.filled <= cfg.parent_sz + leg.n_fills * cfg.parent_sz,
          boost::format("leg filled %.0f against a size of %d")
            % leg.filled % cfg.parent_sz);

  if (leg.filled >= cfg.parent_sz)
  {
    leg.done  = true;
    leg.ended = m->tim ? m->tim : last_tim;
    log_inf("%s leg done: %.0f @ %.2f over %llu ns",
            en::to_string(m->side), leg.filled, leg.vwap(),
            (unsigned long long)(leg.ended - leg.started));
  }
  // Stamp the leg from the FILL, not from the tick that later notices the leg
  // is done. The clock ticks once a second, so taking the end time there
  // quantises every leg to a second -- invisible in a slippage number, fatal
  // to anything per unit time.
  if (m->tim) leg.ended = m->tim;

  log_inf("fill %s %.0f @ %d pos=%d (leg filled=%.0f vwap=%.2f)",
          en::to_string(m->side), sz, m->pxi, position, leg.filled, leg.vwap());

  // Transition HERE, on the fill that completes the leg -- not at the next
  // clock tick. The leg is over the instant the last contract prints, and
  // market volume is accumulated by phase, so leaving the phase set until a
  // tick notices would keep counting volume the leg was not actually working
  // for. At a 1s tick and a 1-lot leg that filled in 0.2s, the denominator
  // covered 5x the interval the numerator did, which deflated participation
  // hardest at exactly the small sizes the size axis is measuring.
  // The window is closed by the clock, never by a fill. A fill can finish a
  // LEG -- that is handled above, where the leg's own size runs out -- but the
  // window runs to the next alarm regardless, because both sides keep quoting.
}

void SlippageProbe::emit_header()
{
  fprintf(out,
          "fire_ts,sym,parent_sz,mid_fire,buy_vwap,buy_filled,buy_fills,buy_ns,"
          "mid_sell,sel_vwap,sel_filled,sel_fills,sel_ns,"
          "slip_buy_ticks,slip_sel_ticks,slip_paired_ticks,slip_legsum_ticks,"
          "buy_leg_mkt_vol,sel_leg_mkt_vol,buy_part,sel_part,"
          "buy_mkt_vwap,sel_mkt_vwap,slip_buy_vs_vwap,slip_sel_vs_vwap,"
          "slip_vs_vwap,"
          "ask_fire,bid_sell,slip_buy_vs_touch,slip_sel_vs_touch,"
          "slip_vs_touch,pos_at_close,outcome\n");
  fflush(out);
}

void SlippageProbe::emit_row(const char *outcome)
{
  // Ticks. mid_* are 2x, so halve them here and nowhere else.
  const double midf = mid_fire / 2.0;
  const double mids = mid_sell / 2.0;
  const double bv   = buy_leg.vwap();
  const double sv   = sel_leg.vwap();

  // Scored against the INTERVAL VWAP instead of the mid.
  //
  // Sign convention matches the mid-based columns: positive is cost. Buying
  // above what everyone else paid over that leg's own window is a cost; selling
  // below it is a cost.
  //
  // This is what separates drift from adverse selection. Drift moves the mid
  // and so moves the mid-based numbers, but it moves the interval VWAP with it
  // -- so a leg that merely rode a trend scores ~0 here, while a leg that was
  // systematically picked off scores positive no matter what the price did.
  // 0 when nothing traded alongside the leg, which the aggregator filters.
  const double bmv = buy_leg.mkt_vwap();
  const double smv = sel_leg.mkt_vwap();
  const double slip_buy_vwap = bmv > 0 ? (bv - bmv) : 0.0;
  const double slip_sel_vwap = smv > 0 ? (smv - sv) : 0.0;
  const double slip_vs_vwap  = (bmv > 0 && smv > 0)
                                 ? (slip_buy_vwap + slip_sel_vwap) / 2.0 : 0.0;

  // Scored against the TOUCH we could have crossed at, at each leg's arrival.
  // Same sign convention: positive is cost. Paying more than the ask we could
  // have lifted is a cost; selling below the bid we could have hit is a cost.
  // A passive algorithm should be NEGATIVE here -- that number is the value of
  // not crossing the spread, and it is the one a trader compares against doing
  // nothing clever at all.
  const double afire = double(ask_fire);
  const double bsell = double(bid_sell);
  const double slip_buy_touch = afire > 0 ? (bv - afire) : 0.0;
  const double slip_sel_touch = bsell > 0 ? (bsell - sv) : 0.0;
  const double slip_vs_touch  = (afire > 0 && bsell > 0)
                                 ? (slip_buy_touch + slip_sel_touch) / 2.0 : 0.0;

  const double slip_buy    = buy_leg.filled > 0 ? bv - midf : 0.0;
  const double slip_sel    = sel_leg.filled > 0 ? mids - sv : 0.0;
  // Half the gap between what we paid and what we received -- the captured
  // spread. Direction-agnostic, and it needs no mid at all, so it cannot be
  // corrupted by mis-capturing one.
  //
  // This is 1/2(cost_buy + cost_sel) only when both legs saw the same mid, and
  // here they DO: both sides quote from the same instant against the same book,
  // which is the whole reason for quoting them together. When the legs were
  // sequential -- buy the parent, then sell it back -- they were separated by
  // however long the buy took, and whatever the market did in between landed in
  // this number as if it were execution cost. At 100 lots that was a ~170s gap
  // and it made this column disagree with slip_legsum by 3x.
  const double slip_paired = (buy_leg.filled > 0 && sel_leg.filled > 0)
                               ? (bv - sv) / 2.0 : 0.0;

  // Each leg against its own contemporaneous mid. Immune to drift between the
  // legs, but it depends on having captured both mids correctly.
  const double slip_legsum = (buy_leg.filled > 0 && sel_leg.filled > 0)
                               ? (slip_buy + slip_sel) / 2.0 : 0.0;

  log_inf("WINDOW %s: buy %.0f@%.2f sel %.0f@%.2f "
          "slip_buy=%.3f slip_sel=%.3f slip_paired=%.3f legsum=%.3f "
          "part_buy=%.4f part_sel=%.4f",
          outcome, buy_leg.filled, bv, sel_leg.filled, sv,
          slip_buy, slip_sel, slip_paired, slip_legsum,
          buy_leg.participation(), sel_leg.participation());

  if (!out) return;
  fprintf(out,
          "%llu,%s,%d,%.2f,%.4f,%.0f,%d,%llu,"
          "%.2f,%.4f,%.0f,%d,%llu,"
          "%.4f,%.4f,%.4f,%.4f,"
          "%.0f,%.0f,%.6f,%.6f,"
          "%.4f,%.4f,%.4f,%.4f,%.4f,"
          "%.2f,%.2f,%.4f,%.4f,%.4f,%d,%s\n",
          (unsigned long long)fire_ts, cfg.sym_name.c_str(), cfg.parent_sz, midf,
          bv, buy_leg.filled, buy_leg.n_fills,
          (unsigned long long)(buy_leg.ended - buy_leg.started),
          mids, sv, sel_leg.filled, sel_leg.n_fills,
          (unsigned long long)(sel_leg.ended - sel_leg.started),
          slip_buy, slip_sel, slip_paired, slip_legsum,
          buy_leg.mkt_vol(), sel_leg.mkt_vol(),
          buy_leg.participation(), sel_leg.participation(),
          bmv, smv, slip_buy_vwap, slip_sel_vwap, slip_vs_vwap,
          afire, bsell, slip_buy_touch, slip_sel_touch, slip_vs_touch,
          position, outcome);
  fflush(out);
}

void SlippageProbe::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  if (!enabled()) return;

  // A window still open when the replay stops is reported, not dropped, and is
  // NOT held to the both-legs-done rule that roll_window asserts: it was cut off
  // by the data running out, not by the book being too thin to fill it.
  if (window_open)
  {
    buy_leg.ended = sel_leg.ended = last_tim;
    emit_row("session_end");
    ++fires_partial;
    window_open = false;
  }

  log_opr("probe summary: %zu windows, %d complete, %d partial",
          n_windows, fires_done, fires_partial);
  fprintf(stderr,
          "SlippageProbe %s: %zu windows, %d complete, %d partial; "
          "ticks=%llu eob=%llu last_tx=%llu bid=%d ask=%d pos=%d\n",
          cfg.sym_name.c_str(), n_windows, fires_done, fires_partial,
          (unsigned long long)n_ticks, (unsigned long long)n_eob,
          (unsigned long long)last_tim, last_bid, last_ask, position);

  if (out) { fclose(out); out = nullptr; }
}

}  // namespace sim
