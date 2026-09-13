/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * SlippageProbe -- the actor that makes the simulator produce a number.
 *
 * THE CONTRACT LIVES IN THE HEADER. sim/act/SlippageProbe.hpp states what a
 * window is, when one ends, what each column means and why the design is shaped
 * this way. It is not repeated here, because the two copies had already drifted
 * apart on where Leg::mark anchors. This note covers only what a reader of the
 * implementation needs before the first function.
 *
 * In one paragraph: the probe runs the shadow lights as a MARKET MAKER. BUY
 * lights are told `target_pos = +sz` and SEL lights `-sz`, once, from one line
 * in on_clock(), and never again. Both sides then quote continuously for the
 * whole session. A periodic alarm polls market time and slices the session into
 * windows; a window closes only when its minimum has elapsed AND both legs have
 * filled their size, and one CSV row is emitted per closed window. Inventory is
 * carried across boundaries, never unwound.
 *
 * The three things most likely to trip up an edit here:
 *
 * 1. **The inventory PINS to a band edge, it does not random-walk around zero.**
 *    Measured over a session: pos > 0 on 69% of fills, exactly zero on 0.5%,
 *    and |pos| at 90% of the band or beyond on 26%. Both sides stay live
 *    because the pinned one is throttled to the remaining distance rather than
 *    switched off, so they churn across the boundary a clip at a time. Any
 *    reasoning here that starts "the position is usually flat" is wrong.
 *
 * 2. **Two clocks meet in fill_handler.** A leg's start is the Timer's alarm
 *    time; a fill's `tim` is the OB's transactTime, stamped when the order was
 *    received -- before the modelled --ob-delay-us, and not strictly monotone
 *    across exchange records either. End stamps are therefore CLAMPED to the
 *    window start, not asserted against it.
 *
 * 3. **A leg can overshoot its parent size by a whole clip.** The clip is
 *    ord_sz from lights.ini, which this actor never sees and which the grid
 *    does not override, so at --probe-size 1 or 2 one legitimate clip exceeds
 *    the parent by itself. Do not assert an overshoot bound here.
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

  // tick_s is the POLL, min_window_ns is the window. Printing the former under
  // the latter's name read "window=1s" against a 900s minimum.
  fprintf(stderr, "probe: armed on %s, min window=%llus, poll=%ds, session=%llu..%llu\n",
          cfg.sym_name.c_str(),
          (unsigned long long)(cfg.min_window_ns / 1000000000ull), cfg.tick_s,
          (unsigned long long)cfg.session_start,
          (unsigned long long)cfg.session_end);
  log_inf("probe armed: sym=%s parent_sz=%d min_window=%llus poll=%ds -- targets "
          "are NOT sent here, the first alarm inside the session sends them",
          cfg.sym_name.c_str(), cfg.parent_sz,
          (unsigned long long)(cfg.min_window_ns / 1000000000ull), cfg.tick_s);
}

void SlippageProbe::give_work() noexcept
{
  // THE ONLY PLACE THE PROBE TOUCHES A POSITION, and it sends no target at all.
  //
  // targetpos stays 0 on every light. A light with targetpos 0 works only while
  // its book is on the far side of flat (light22.hpp:239,243) -- a BUY light
  // while pos < 0, a SEL light while pos > 0 -- so the way to give a side a
  // parent to execute is to tell its book it holds the OPPOSITE position. The
  // lights then work it back to flat and stop by themselves. That is what
  // PositionManager does (PositionManager.hpp:56) and it is why there is no
  // flattening step anywhere in this actor: nothing has to unwind a position
  // that was only ever a piece of work.
  //
  // Each side has its own book because no single position leaves both sides
  // live: at -sz only the buy side works, at +sz only the sell side, at 0
  // neither. One shared book runs one leg at a time, which is the sequential
  // design this replaces.
  ASSERT(pcoord_buy, "no buy position book: the buy side would never bid");
  ASSERT(pcoord_sel, "no sel position book: the sell side would never offer");
  ASSERTF(cfg.parent_sz > 0, boost::format("parent_sz %d") % cfg.parent_sz);

  const int sz = cfg.parent_sz;

  // One parent per window per side, added -- never topped up to a level. A side
  // that overshoots on its last clip (the lights net into a SHARED position
  // while each throttles against its OWN working size) simply has that much
  // less to do next window. That self-corrects and must not be adjusted away.
  //
  // The exception is a side that has RUN AHEAD by more than a whole parent,
  // which happens whenever ord_sz exceeds parent_sz -- 5 against 1 or 2 is
  // exactly what the grid runs. One parent then does not put it back to work at
  // all: it would have nothing to do, its leg could never fill, and the window
  // could never close. So keep handing whole parents until the side actually
  // has work. The unit is still one parent and the totals stay exact; the extra
  // rounds are the windows that side already executed in advance.
  auto give = [&](light::PCoord *pc, en::bs opposite, int &given, const char *what) {
    const bool buy_side = (opposite == en::bs::SEL);
    int rounds = 0;
    for (;;)
    {
      pc->add_position(opposite, sz);
      given += sz;
      ++rounds;
      const int p = pc->get_position();
      const bool working = buy_side ? (p < 0) : (p > 0);
      if (working) break;
      ASSERTF(rounds < 1000,
              boost::format("%s side never takes work: book %d after %d parents")
                % what % p % rounds);
    }
    if (rounds > 1)
      log_opr("%s side had run %d parent(s) ahead; gave it %d this window",
              what, rounds - 1, rounds);
  };

  give(pcoord_buy, en::bs::SEL, work_given_buy, "buy");   // short -> BUY lights work
  give(pcoord_sel, en::bs::BUY, work_given_sel, "sel");   // long  -> SEL lights work

  const int pb = pcoord_buy->get_position();
  const int ps = pcoord_sel->get_position();
  ASSERTF(pb < 0, boost::format("buy book at %d: its lights are idle") % pb);
  ASSERTF(ps > 0, boost::format("sel book at %d: its lights are idle") % ps);

  log_opr("window %zu: buy book %d (%d given), sel book %d (%d given)",
          n_windows, pb, work_given_buy, ps, work_given_sel);
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

  fire_ts   = now;
  mid_fire  = last_bid + last_ask;    // 2x the mid, kept integral
  ask_fire  = last_ask;               // what crossing would have cost the buy
  bid_fire  = last_bid;               // what crossing would have got the sell
  // Both legs arrive HERE, together. There is deliberately no second anchor:
  // the old sequential design bought the parent and only then began selling, so
  // the sell side had an arrival of its own, minutes later. Quoting both sides
  // at once removes that -- and with it the drift BETWEEN the arrivals, which
  // is why drift is now measured forward from this one instant to where each
  // leg actually finished (buy_end_mid / sel_end_mid / mid_close) rather than
  // as the gap between two arrivals.
  mid_close = 0;

  pos_max = pos_min = position;   // the extremes are per window, not per session
  pos_integral = abs_integral = 0;
  pos_last_tim = now;

  buy_leg.reset();
  sel_leg.reset();
  buy_leg.started = sel_leg.started = now;
  // Anchor both volume denominators HERE, at window open, so each leg's
  // market-volume interval matches the duration it reports. Anchoring on the
  // first fill instead made participation divide by a shorter span than
  // buy_ns/sel_ns described.
  buy_leg.mark(mkt_vol_cum, mkt_not_cum, mkt_vol_hit, mkt_not_hit);
  sel_leg.mark(mkt_vol_cum, mkt_not_cum, mkt_vol_tak, mkt_not_tak);

  window_open   = true;
  stall_warn_at = 0;
  ++n_windows;

  // Hand both sides another parent to execute. THE ONLY CALL SITE.
  give_work();

  // What each side has to execute this window: everything handed out so far,
  // less everything it has already filled. That is one parent plus or minus the
  // last window's overshoot, and it is the number a leg is measured against.
  buy_leg.to_do = int(double(work_given_buy) - buy_filled_total);
  sel_leg.to_do = int(double(work_given_sel) - sel_filled_total);
  ASSERTF(buy_leg.to_do > 0, boost::format("buy leg has %d to do") % buy_leg.to_do);
  ASSERTF(sel_leg.to_do > 0, boost::format("sel leg has %d to do") % sel_leg.to_do);

  log_inf("WINDOW %zu at tx=%llu mid=%.1f pos=%d",
          n_windows, (unsigned long long)now, mid_fire / 2.0, position);
}

void SlippageProbe::close_window(uint64_t now) noexcept
{
  // Only ever called with a window open -- on_clock decides that.
  ASSERT(window_open, "close_window with no window open");

  // Both legs filled their size -- on_clock will not call this otherwise, it
  // waits. Each keeps the stamp of the fill that completed it, so a leg done in
  // 10s is measured over 10s even though the window ran for minutes.
  ASSERT(buy_leg.done && sel_leg.done, "close_window with an unfinished leg");
  ASSERTF(now >= fire_ts + cfg.min_window_ns,
          boost::format("window %zu closed after %llu ns, under the %llu ns minimum")
            % n_windows % (unsigned long long)(now - fire_ts)
            % (unsigned long long)cfg.min_window_ns);

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
  // Close the last inventory segment at the boundary, so the integral covers
  // the whole window rather than stopping at the final fill.
  accrue_inventory(now);

  // The mid the window ENDED on. mid_close - mid_fire is the drift the pair
  // lived through, and it is a real measurement now rather than the identically
  // zero difference of two anchors taken at the same instant.
  mid_close = last_bid + last_ask;

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
  if (now < cfg.session_start) return;          // before the session we measure
  if (last_bid <= 0 || last_ask <= 0) return;   // need a touch to anchor the row

  const bool in_session = now < cfg.session_end;

  if (!window_open)
  {
    // Either the first boundary of the session, or the session is over and the
    // last window is already closed.
    if (!in_session) return;

    // open_window hands both sides their work -- one call site, once per
    // window. Nothing is sent at the end of the session: there is no target to
    // clear, and a book still being worked is the honest state to leave it in.
    open_window(now);
    return;
  }

  // A window ends when BOTH conditions hold: its minimum has elapsed, and both
  // legs have filled their size.
  //
  // The minimum is a floor, not a deadline. Each leg is already measured over
  // its OWN interval -- it stops accumulating at the fill that completed it, so
  // a leg done in 10s is measured over 10s -- and once both are done the window
  // simply idles out the remainder while the lights keep quoting unmeasured.
  //
  // If a leg has NOT filled when the minimum elapses, the window stays open
  // until it does. That is the whole reason the previous fixed-period design
  // was wrong: it closed on the clock regardless, so a slow leg produced a row
  // reporting a partial execution as a complete one. Asserting instead was no
  // better -- it turned a thin session into a dead run, and an aborted run
  // leaves a truncated CSV that the resume guard and the aggregator both accept
  // as complete, biasing the corpus toward the benign sessions.
  const bool min_elapsed = now >= fire_ts + cfg.min_window_ns;
  if (!min_elapsed || !buy_leg.done || !sel_leg.done)
  {
    // Waiting is correct, but it must not be SILENT. A leg that never fills
    // holds this window open for the rest of the session, and every later
    // window simply never happens: the run then ends with one row, which
    // run_grid.sh's `wc -l > 1` resume guard accepts as a completed cell and
    // never retries. Say so, once per minimum elapsed, so a stalled cell is
    // visible in the log and in the session_end row's outcome.
    if (min_elapsed)
    {
      const uint64_t over = now - (fire_ts + cfg.min_window_ns);
      if (over >= stall_warn_at)
      {
        stall_warn_at = over + cfg.min_window_ns;
        log_err("window %zu STALLED: %llus past the minimum, buy %.0f/%d sel %.0f/%d "
                "-- still waiting, no later window can open until both fill",
                n_windows, (unsigned long long)(over / 1000000000ull),
                buy_leg.filled, cfg.parent_sz, sel_leg.filled, cfg.parent_sz);
      }
    }
    return;
  }

  close_window(now);

  // Only open a window that can COMPLETE inside the session. Reopening at
  // 15:29 against a 15-minute minimum produced a window that could not close
  // before 15:44, was emitted "ok", and drew its VWAPs and participation
  // denominators from post-close liquidity -- a different regime from the RTH
  // session the corpus is meant to characterise. The lights keep quoting after
  // this point; nothing more is MEASURED.
  if (now + cfg.min_window_ns <= cfg.session_end)
    open_window(now);
  else
    log_opr("session %llu: no room for another window, %llus left against a %llus "
            "minimum -- measurement stops here, the lights keep quoting",
            (unsigned long long)now,
            (unsigned long long)((cfg.session_end > now ? cfg.session_end - now : 0) / 1000000000ull),
            (unsigned long long)(cfg.min_window_ns / 1000000000ull));
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
  const double tsz = double(m->payload->sz);
  const double tnot = tsz * double(m->payload->px.to_int());
  mkt_vol_cum += tsz;
  mkt_not_cum += tnot;

  // Split by AGGRESSOR for the hit/take benchmark. `side` on a trade is the
  // RESTING order's side, so is_hit() means a bid was hit (the aggressor sold)
  // and is_tak() means an offer was taken (the aggressor bought) -- Data.hpp.
  // Those are the trades our own resting orders were competing with: our buy
  // leg sits on the bid and is filled by a hitter, so the hits are the other
  // bids that got filled while we waited.
  if (m->payload->is_hit())      { mkt_vol_hit += tsz; mkt_not_hit += tnot; }
  else if (m->payload->is_tak()) { mkt_vol_tak += tsz; mkt_not_tak += tnot; }
}

// Add the position we have been carrying since the last move to the integrals.
//
// Called at every fill and again at the close, so the window is covered
// end to end with no gap: each segment contributes pos * (now - last), and the
// final segment runs from the last fill to the boundary.
void SlippageProbe::accrue_inventory(uint64_t now) noexcept
{
  if (!window_open) return;
  if (pos_last_tim == 0) pos_last_tim = fire_ts;
  // Time can arrive out of order across the two clocks (a fill is stamped
  // before the modelled delay); a negative segment would subtract risk that was
  // really carried, so clamp rather than trust the difference.
  if (now > pos_last_tim)
  {
    const double dt = double(now - pos_last_tim);
    pos_integral += double(position) * dt;
    abs_integral += std::abs(double(position)) * dt;
    // Only advance on a segment we actually accrued. Rewinding on an
    // out-of-order fill -- which the two clocks make routine -- would make the
    // NEXT segment span the gap again and double-count it, compounding with
    // every such fill.
    pos_last_tim = now;
  }
}

void SlippageProbe::fill_handler(const frame::som::msg::Fill *m) noexcept
{
  if (!enabled()) return;
  if (m->sym != cfg.sym) return;

  const double px = m->pxi;
  const double sz = m->sz;
  ASSERTF(sz > 0, boost::format("fill with size %.0f") % sz);
  ASSERTF(px > 0, boost::format("fill at price %.0f") % px);

  // Integrate the OLD position over the time it was held, then move it. Order
  // matters: the position we carried since the last fill is the one that was at
  // risk over that interval, not the one we are about to have.
  accrue_inventory(m->tim ? m->tim : last_tim);

  if (m->side == en::bs::BUY) position += int(sz);
  else                        position -= int(sz);
  if (position > pos_max) pos_max = position;
  if (position < pos_min) pos_min = position;

  // Side identifies the light exactly: a light22<BUY> only ever bids and a
  // light22<SEL> only ever offers. Nothing unwinds a position here, so there is
  // no case where a light trades against its own side and the attribution
  // inverts.
  Leg &leg = (m->side == en::bs::BUY) ? buy_leg : sel_leg;

  // THE SESSION TOTAL COUNTS EVERY FILL, INCLUDING THE OVERSHOOT.
  //
  // This must come BEFORE the leg.done guard below. A side overshoots its work
  // on the last clip, and those extra lots are real: they moved the position
  // book, so the next window starts with that much less to do. If they are not
  // counted here, to_do for the next window is computed too large and the leg
  // asks for more than the lights are holding -- which stalls the window
  // permanently, the same failure that cost 22 of 24 windows once already:
  //
  //   sz=10, W1: to_do 10, book -10, lights fill 13, book ends +3
  //          W2: book -10+3 = -7, so only 7 lots of work exist
  //              miscounted:  to_do = 20 - 10 = 10  -> never reachable, STALL
  //              counted:     to_do = 20 - 13 =  7  -> exactly right
  if (m->side == en::bs::BUY) buy_filled_total += sz;
  else                        sel_filled_total += sz;

  // OVERFILLS ARE DELIBERATELY EXCLUDED FROM THE VWAP. This is the decision,
  // not an accident of where the guard sits.
  //
  // A window asks a side to execute exactly to_do lots. The lights can deliver
  // more -- they net into a SHARED position while each throttles against its
  // OWN working size, so two of them can fill the last of the work at once --
  // and those extra lots are NOT part of the job this window was measuring.
  // Letting them into buy_vwap/sel_vwap would pollute the number with
  // executions the experiment never asked for, at whatever price happened to be
  // there after the leg was already complete. The row must describe the parent
  // it was given, and nothing else.
  //
  // Nothing is lost or double-counted. The lots are in the session totals above,
  // so they come straight off the next window's to_do and the position books
  // stay exact -- they are excluded from the MEASUREMENT, not from the
  // accounting. Measured over a session they are rare: 5 lots in 210, on 3
  // windows in 24.
  //
  // TODO: the price paid on those lots is real and currently unrecorded. Worth
  // a column one day -- an overfill executed into a moving market has a cost,
  // and knowing it would bound how much the exclusion flatters the result. Left
  // out for now because it is rare enough not to change any conclusion, and
  // including it half-way (in the size but not the price, or the reverse) would
  // be worse than leaving it out cleanly.
  //
  // This guard is also what stops completion re-triggering: once leg.done is
  // set, every later fill on that side returns here, so the "is it finished"
  // test below runs at most once per leg per window. An overshoot that carries
  // the book to 0, then +1, then +5 cannot fire it again, and close_window sees
  // one end stamp rather than the last fill to arrive.
  if (leg.done) return;
  leg.notional += px * sz;
  leg.filled   += sz;
  ++leg.n_fills;
  // Opens this leg's interval on its first fill and extends it on every one
  // after, so its market-volume denominator spans exactly the time it was
  // actually trading.
  if (m->side == en::bs::BUY) leg.mark(mkt_vol_cum, mkt_not_cum, mkt_vol_hit, mkt_not_hit);
  else                        leg.mark(mkt_vol_cum, mkt_not_cum, mkt_vol_tak, mkt_not_tak);

  // A LEG IS FINISHED WHEN IT HAS EXECUTED THE WORK THIS WINDOW GAVE IT --
  // leg.to_do, not cfg.parent_sz.
  //
  // The two differ whenever the previous window did not land exactly on flat.
  // A side overshoots on its last clip (the lights net against a SHARED
  // position while each throttles against its OWN working size), so a book can
  // finish a window slightly past zero and the next window starts from there.
  // Testing against parent_sz then stalls the session outright: window 1 ended
  // at +3 on a 10-lot parent, window 2 therefore had only 7 lots of work, and a
  // 10-lot completion test could never be satisfied -- the window stayed open
  // for the rest of the day and 22 of 24 windows never happened.
  //
  // to_do is computed from this actor's OWN running totals at window open
  // (work_given minus what the side has filled all session), never from the
  // PCoord. The book is updated by the light's fill handler, and this handler is
  // a separate dispatch off the same SOM publish -- reading the book here would
  // make completion depend on which subscriber SOM happened to serve first.
  // `>=`, NOT `==`.
  //
  // leg.filled jumps by the size of the fill that just landed, so it can step
  // straight PAST to_do without ever being equal to it: a leg with 2 lots left
  // takes a 5-lot clip and goes from 8 to 13 against a to_do of 10. The lights
  // throttle each clip to the work remaining (light22_base.hpp:857 mins against
  // diff_from_target), but that throttle is computed per light against its own
  // working size while the position they net into is shared, so N lights can
  // each be resting up to the full remainder and two of them can fill at once.
  //
  // With `==` the equality is simply never observed on those windows, the leg
  // is never marked done, and since a finished side stops quoting there is no
  // later fill to re-test -- the window stays open for the rest of the session
  // and every window after it never happens. That is not hypothetical; it is
  // the failure this code path already produced once, with 22 of 24 windows
  // lost. The same reasoning is why the completion test is "at or past", not
  // "exactly at", anywhere it appears.
  if (leg.filled >= leg.to_do)
  {
    leg.done = true;
    // TWO CLOCKS MEET HERE. `started` is the Timer's alarm time; the fill's
    // `tim` is the OB's transactTime, stamped when the order was RECEIVED --
    // before the modelled --ob-delay-us. An order submitted just before the
    // boundary and released after it therefore carries a stamp EARLIER than the
    // window that owns it, and exchange transactTimes are not strictly monotone
    // either (OB.cpp:2360). Clamp rather than assert: a leg cannot have ended
    // before it began, and the error is bounded by the delay model.
    const uint64_t stamp = m->tim ? m->tim : last_tim;
    leg.ended   = stamp > leg.started ? stamp : leg.started;
    // The mid this leg finished on, for its own drift.
    leg.end_mid = last_bid + last_ask;
    log_inf("%s leg done: filled %.0f of %d @ %.2f over %llu ns",
            en::to_string(m->side), leg.filled, leg.to_do, leg.vwap(),
            (unsigned long long)(leg.ended - leg.started));
  }
  // Stamp the leg from the FILL, not from the tick that later notices the leg
  // is done. The clock ticks once a second, so taking the end time there
  // quantises every leg to a second -- invisible in a slippage number, fatal
  // to anything per unit time. Clamped to the window's start for the same
  // two-clock reason as the completion stamp above, and monotone so it always
  // names the latest fill this leg has seen.
  if (m->tim && m->tim > leg.ended)
    leg.ended = m->tim > leg.started ? m->tim : leg.started;

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
          "sel_vwap,sel_filled,sel_fills,sel_ns,"
          "slip_buy_ticks,slip_sel_ticks,slip_paired_ticks,"
          "buy_drift_ticks,sel_drift_ticks,drift_ticks,"
          "buy_leg_mkt_vol,sel_leg_mkt_vol,buy_part,sel_part,"
          "buy_mkt_vwap,sel_mkt_vwap,slip_buy_vs_vwap,slip_sel_vs_vwap,"
          "slip_vs_vwap,"
          "buy_hit_vol,sel_tak_vol,buy_hit_vwap,sel_tak_vwap,"
          "slip_buy_vs_hit,slip_sel_vs_tak,slip_vs_agg,"
          "ask_fire,bid_sell,slip_buy_vs_touch,slip_sel_vs_touch,"
          "slip_vs_touch,"
          "pos_at_close,pos_max,pos_min,pos_mean,pos_abs_mean,outcome\n");
  fflush(out);
}

void SlippageProbe::emit_row(const char *outcome)
{
  // Ticks. mid_* are 2x, so halve them here and nowhere else.
  const double midf = mid_fire / 2.0;
  // Where the mid actually went. Both legs arrived at midf together, so there
  // is no "sell arrival" to difference against it -- the drift that matters is
  // forward, from the common arrival to where each leg finished and to where
  // the window closed. A leg that never finished reports no drift rather than
  // a drift measured to a mid it never saw.
  const double buy_drift = (buy_leg.done && buy_leg.end_mid > 0)
                             ? (buy_leg.end_mid - mid_fire) / 2.0 : 0.0;
  const double sel_drift = (sel_leg.done && sel_leg.end_mid > 0)
                             ? (sel_leg.end_mid - mid_fire) / 2.0 : 0.0;
  const double drift     = mid_close > 0 ? (mid_close - mid_fire) / 2.0 : 0.0;
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
  // Scored against the trades this leg was COMPETING WITH, split by aggressor.
  //
  // Our buy leg rests on the bid; it fills when someone hits that bid. The other
  // trades that printed against resting bids over the same interval are the
  // other passive buyers who got filled while we waited -- that is the peer
  // group, and it answers a sharper question than the all-trades VWAP does.
  // vs_vwap asks "did we beat the average trade", which includes everyone who
  // crossed the spread and therefore embeds the spread itself. vs_hit asks "did
  // we beat the other resting bids", which does not.
  //
  // Sign convention as everywhere: positive is cost. Buying above what other
  // hit bids paid is a cost; selling below what other taken offers received is
  // a cost. 0 when no trade of that aggressor landed beside the leg.
  // The VOLUME of each leg's peer group is emitted alongside its VWAP, because
  // the VWAP alone cannot say how thin the comparison was. It is also the right
  // denominator for a participation rate against that peer group: our buy leg
  // competed with buy_hit_vol lots of other filled bids, not with every lot
  // that traded, so ours/(ours + hits) is the share of PASSIVE BUYING we took
  // -- a different and more honest number than ours/(ours + all trades), which
  // dilutes us with every aggressor on both sides.
  // Time-weighted inventory over the window. The signed mean says which way we
  // leaned; the absolute mean says how much risk was actually carried, and they
  // differ sharply when the book swings through zero -- which is the normal
  // case here, since the two sides are worked against each other.
  const double span = (last_tim > fire_ts) ? double(last_tim - fire_ts) : 0.0;
  const double pos_mean     = span > 0 ? pos_integral / span : 0.0;
  const double pos_abs_mean = span > 0 ? abs_integral / span : 0.0;

  const double bhv = buy_leg.agg_vwap();      // VWAP of bids that were HIT
  const double stv = sel_leg.agg_vwap();      // VWAP of offers that were TAKEN
  const double slip_buy_hit = (bhv > 0 && buy_leg.filled > 0) ? (bv - bhv) : 0.0;
  const double slip_sel_tak = (stv > 0 && sel_leg.filled > 0) ? (stv - sv) : 0.0;
  const double slip_vs_agg  = (bhv > 0 && stv > 0 && buy_leg.filled > 0
                                 && sel_leg.filled > 0)
                                ? (slip_buy_hit + slip_sel_tak) / 2.0 : 0.0;

  const double bmv = buy_leg.mkt_vwap();
  const double smv = sel_leg.mkt_vwap();
  const double slip_buy_vwap = (bmv > 0 && buy_leg.filled > 0) ? (bv - bmv) : 0.0;
  const double slip_sel_vwap = (smv > 0 && sel_leg.filled > 0) ? (smv - sv) : 0.0;
  const double slip_vs_vwap  = (bmv > 0 && smv > 0 && buy_leg.filled > 0
                                  && sel_leg.filled > 0)
                                 ? (slip_buy_vwap + slip_sel_vwap) / 2.0 : 0.0;

  // Scored against the TOUCH we could have crossed at, at each leg's arrival.
  // Same sign convention: positive is cost. Paying more than the ask we could
  // have lifted is a cost; selling below the bid we could have hit is a cost.
  // A passive algorithm should be NEGATIVE here -- that number is the value of
  // not crossing the spread, and it is the one a trader compares against doing
  // nothing clever at all.
  //
  // Guarded on OUR leg having filled as well as on the reference existing.
  // vwap() returns 0 for an unfilled leg, so a session_end row with a short
  // sell leg scored bid_fire - 0 ~ +23990 ticks into a column whose real values
  // are fractions of a tick.
  const double afire = double(ask_fire);
  const double bsell = double(bid_fire);
  const double slip_buy_touch = (afire > 0 && buy_leg.filled > 0) ? (bv - afire) : 0.0;
  const double slip_sel_touch = (bsell > 0 && sel_leg.filled > 0) ? (bsell - sv) : 0.0;
  const double slip_vs_touch  = (afire > 0 && bsell > 0 && buy_leg.filled > 0
                                   && sel_leg.filled > 0)
                                 ? (slip_buy_touch + slip_sel_touch) / 2.0 : 0.0;

  const double slip_buy    = buy_leg.filled > 0 ? bv - midf : 0.0;
  const double slip_sel    = sel_leg.filled > 0 ? midf - sv : 0.0;
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

  // slip_legsum USED TO LIVE HERE and has been removed, not renamed.
  //
  // It was (slip_buy + slip_sel)/2 = ((bv - mid_fire) + (mid_sell - sv))/2, and
  // it earned its place only while mid_sell was a SECOND arrival taken when the
  // sell leg began, minutes after the buy. Quoting both sides together makes
  // mid_sell == mid_fire by construction, at which point the expression reduces
  // to (bv - sv)/2 -- exactly slip_paired, to the last bit. Two columns, one
  // number, printed with independent confidence intervals as though they were
  // evidence of each other. The drift it was there to expose is now measured
  // directly, forward, as buy_drift / sel_drift / drift.

  log_inf("WINDOW %s: buy %.0f@%.2f sel %.0f@%.2f "
          "slip_buy=%.3f slip_sel=%.3f slip_paired=%.3f drift=%.3f "
          "part_buy=%.4f part_sel=%.4f",
          outcome, buy_leg.filled, bv, sel_leg.filled, sv,
          slip_buy, slip_sel, slip_paired, drift,
          buy_leg.participation(), sel_leg.participation());

  if (!out) return;
  fprintf(out,
          "%llu,%s,%d,%.2f,%.4f,%.0f,%d,%llu,"
          "%.4f,%.0f,%d,%llu,"
          "%.4f,%.4f,%.4f,"
          "%.4f,%.4f,%.4f,"
          "%.0f,%.0f,%.6f,%.6f,"
          "%.4f,%.4f,%.4f,%.4f,%.4f,"
          "%.0f,%.0f,%.4f,%.4f,%.4f,%.4f,%.4f,"
          "%.2f,%.2f,%.4f,%.4f,%.4f,"
          "%d,%d,%d,%.3f,%.3f,%s\n",
          (unsigned long long)fire_ts, cfg.sym_name.c_str(), cfg.parent_sz, midf,
          bv, buy_leg.filled, buy_leg.n_fills,
          (unsigned long long)(buy_leg.ended - buy_leg.started),
          sv, sel_leg.filled, sel_leg.n_fills,
          (unsigned long long)(sel_leg.ended - sel_leg.started),
          slip_buy, slip_sel, slip_paired,
          buy_drift, sel_drift, drift,
          buy_leg.mkt_vol(), sel_leg.mkt_vol(),
          buy_leg.participation(), sel_leg.participation(),
          bmv, smv, slip_buy_vwap, slip_sel_vwap, slip_vs_vwap,
          buy_leg.agg_vol(), sel_leg.agg_vol(),
          bhv, stv, slip_buy_hit, slip_sel_tak, slip_vs_agg,
          afire, bsell, slip_buy_touch, slip_sel_touch, slip_vs_touch,
          position, pos_max, pos_min, pos_mean, pos_abs_mean, outcome);
  fflush(out);
}

void SlippageProbe::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  if (!enabled()) return;

  // A window still open when the replay stops is reported, not dropped, and is
  // NOT held to the both-legs-done rule that close_window asserts: it was cut off
  // by the data running out, not by the book being too thin to fill it.
  if (window_open)
  {
    // Only a leg that never finished gets the cut-off stamp. Restamping a leg
    // that completed 12 minutes earlier reported it as having worked the whole
    // window while its volume denominator stayed frozen at its real last fill,
    // so buy_ns and buy_leg_mkt_vol described different intervals in one row.
    if (!buy_leg.done) buy_leg.ended = last_tim;
    if (!sel_leg.done) sel_leg.ended = last_tim;
    // Close the final inventory segment before the row is written. Without it
    // the integral stops at the last fill while span covers the whole window,
    // so a window that stalled for hours holding a position reports almost no
    // inventory -- the exact rows where it is highest.
    accrue_inventory(last_tim);
    mid_close = last_bid + last_ask;

    // Name the failure. "session_end" alone could not distinguish "the replay
    // ran out of data" from "the book was too thin to fill this leg all
    // afternoon", which are the two cases the sweep most needs to tell apart --
    // and it is the second one that silently truncates a cell's whole corpus.
    const char *outcome = (!buy_leg.done && !sel_leg.done) ? "both_short"
                        : !buy_leg.done                    ? "buy_short"
                        : !sel_leg.done                    ? "sel_short"
                                                           : "session_end";
    emit_row(outcome);
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
