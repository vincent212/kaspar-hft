/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
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
    log_inf("probe disabled (parent_sz=%d fires=%zu)", cfg.parent_sz, cfg.fire_ts.size());
    return;
  }

  // The Timer is the clock. A relative periodic alarm, not the absolute
  // AlarmClockSub(h,m,s,ms,id) form -- that one is UTC (chutil::Time formats
  // with gmtime_r) and would slip an hour against ET at the DST change inside
  // the test window. Relative has no timezone in it, and the Timer runs on
  // market time, so a tick is a tick of the session.
  ASSERT(timer, "probe has no timer");
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

  fprintf(stderr, "probe: armed on %s, %zu fires, first=%llu\n",
          cfg.sym_name.c_str(), cfg.fire_ts.size(),
          (unsigned long long)cfg.fire_ts.front());
  log_inf("probe armed: sym=%s parent_sz=%d fires=%zu leg_timeout=%llus",
          cfg.sym_name.c_str(), cfg.parent_sz, cfg.fire_ts.size(),
          (unsigned long long)(cfg.leg_timeout_ns / 1000000000ull));
}

void SlippageProbe::set_target(int n) noexcept
{
  // Both lights get the same target. targetpos is what arbitrates between
  // them: BUY stands down at pos >= targetpos, SEL at pos <= targetpos.
  if (light_buy) light_buy->send(new light::msg::Set(light::msg::Set::TARGET_POS, n), this);
  if (light_sel) light_sel->send(new light::msg::Set(light::msg::Set::TARGET_POS, n), this);
}

void SlippageProbe::begin_fire(uint64_t now) noexcept
{
  fire_ts  = cfg.fire_ts[next_fire];
  mid_fire = last_bid + last_ask;     // 2x the mid, kept integral
  mid_sell = 0;
  ask_fire = last_ask;                // what crossing would have cost us
  bid_fire = last_bid;
  bid_sell = ask_sell = 0;
  buy_leg.reset();
  sel_leg.reset();
  buy_leg.started = now;
  phase = Phase::BUYING;
  ++next_fire;

  log_inf("FIRE %zu at tx=%llu mid=%.1f -> buying %d",
          next_fire, (unsigned long long)now, mid_fire / 2.0, cfg.parent_sz);
  fprintf(stderr, "probe: FIRE %zu/%zu tx=%llu mid=%.1f buying %d\n",
          next_fire, cfg.fire_ts.size(), (unsigned long long)now,
          mid_fire / 2.0, cfg.parent_sz);
  set_target(cfg.parent_sz);
}

void SlippageProbe::begin_sell_leg(uint64_t now) noexcept
{
  if (!buy_leg.ended) buy_leg.ended = now;   // no fills: fall back to the tick
  mid_sell        = last_bid + last_ask;
  bid_sell        = last_bid;         // what crossing would have got us
  ask_sell        = last_ask;
  sel_leg.started = now;
  phase = Phase::SELLING;

  log_inf("fire %zu buy leg done: filled=%.0f vwap=%.2f -> selling back",
          next_fire, buy_leg.filled, buy_leg.vwap());
  set_target(0);
}

void SlippageProbe::finish_fire(uint64_t now, const char *why) noexcept
{
  if (!sel_leg.ended) sel_leg.ended = now;
  emit_row(why);

  if (strcmp(why, "ok") == 0) ++fires_done;
  else                        ++fires_partial;

  // Always leave the lights flat: a fire that ends holding inventory would
  // corrupt the next one, which measures from zero.
  set_target(0);
  phase = Phase::WAITING;
}

// Prices only. Time comes from the Timer.
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
  switch (phase)
  {
    case Phase::BUYING:
      if (buy_leg.filled >= cfg.parent_sz)
        begin_sell_leg(now);
      else if (now - buy_leg.started > cfg.leg_timeout_ns)
      {
        log_inf("fire %zu buy leg timed out with %.0f/%d", next_fire, buy_leg.filled, cfg.parent_sz);
        begin_sell_leg(now);
      }
      break;

    case Phase::SELLING:
      if (position <= 0)
        finish_fire(now, buy_leg.filled >= cfg.parent_sz ? "ok" : "buy_short");
      else if (now - sel_leg.started > cfg.leg_timeout_ns)
        finish_fire(now, "sel_short");
      break;

    case Phase::WAITING:
      break;
  }

  // Start the next fire only from flat and only with a usable touch: a fire
  // that begins with inventory, or with no mid to measure against, produces a
  // number that cannot be interpreted.
  while (phase == Phase::WAITING && next_fire < cfg.fire_ts.size() &&
         now >= cfg.fire_ts[next_fire])
  {
    if (position != 0)
    {
      log_err("skipping fire %zu: not flat (pos=%d)", next_fire, position);
      ++fires_skipped;
      ++next_fire;
      set_target(0);
      continue;
    }
    if (last_bid <= 0 || last_ask <= 0)
    {
      log_err("skipping fire %zu: no two-sided book yet", next_fire);
      ++fires_skipped;
      ++next_fire;
      continue;
    }
    begin_fire(now);
  }
}

// Every execution in the market, ours or anyone's. Counted into whichever leg
// is working so that participation is measured over exactly the window we were
// actually trying to trade in.
void SlippageProbe::trade_handler(const frame::ob::msg::TradeNotify *m) noexcept
{
  if (!enabled() || !m->payload) return;
  if (uint32_t(m->payload->sym) != cfg.sym) return;

  // Size AND notional: the price is on the payload and was previously thrown
  // away, which left the interval VWAP -- the benchmark that separates drift
  // from adverse selection -- unobtainable from the CSV.
  const double tpx = double(m->payload->px.to_int());
  const double tsz = double(m->payload->sz);
  if (phase == Phase::BUYING)
  {
    buy_leg.mkt_vol      += tsz;
    buy_leg.mkt_notional += tsz * tpx;
  }
  else if (phase == Phase::SELLING)
  {
    sel_leg.mkt_vol      += tsz;
    sel_leg.mkt_notional += tsz * tpx;
  }
}

void SlippageProbe::fill_handler(const frame::som::msg::Fill *m) noexcept
{
  if (!enabled()) return;
  if (m->sym != cfg.sym) return;

  const double px = m->pxi;
  const double sz = m->sz;

  if (m->side == en::bs::BUY) position += int(sz);
  else                        position -= int(sz);

  Leg &leg = (m->side == en::bs::BUY) ? buy_leg : sel_leg;
  leg.notional += px * sz;
  leg.filled   += sz;
  ++leg.n_fills;
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
  const uint64_t now = m->tim ? m->tim : last_tim;
  if (phase == Phase::BUYING && buy_leg.filled >= cfg.parent_sz)
    begin_sell_leg(now);
  else if (phase == Phase::SELLING && position <= 0)
    finish_fire(now, buy_leg.filled >= cfg.parent_sz ? "ok" : "buy_short");
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
          "slip_vs_touch,outcome\n");
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
  // above what everyone else paid over the same window is a cost; selling below
  // it is a cost.
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
  // The paper's definition: half the gap between what the buyer paid and what
  // the seller received. Direction-agnostic, and it needs no mid at all --
  // which is the point, since it cannot be corrupted by mis-capturing one.
  //
  // Algebraically this is 1/2(cost_buy + cost_sel) only when both legs saw the
  // same mid. Our legs are sequential, so they differ by whatever the market
  // did in between: measured at -0.097 ticks mean over 3,018 round trips,
  // moving the result by 0.049. Small, but it is market drift rather than
  // execution cost, so slip_legsum below reports the drift-free form alongside
  // and the gap between the two columns IS the drift term.
  const double slip_paired = (buy_leg.filled > 0 && sel_leg.filled > 0)
                               ? (bv - sv) / 2.0 : 0.0;

  // Each leg against its own contemporaneous mid. Immune to drift between the
  // legs, but it depends on having captured both mids correctly.
  const double slip_legsum = (buy_leg.filled > 0 && sel_leg.filled > 0)
                               ? (slip_buy + slip_sel) / 2.0 : 0.0;

  log_inf("FIRE DONE %s: buy %.0f@%.2f sel %.0f@%.2f "
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
          "%.2f,%.2f,%.4f,%.4f,%.4f,%s\n",
          (unsigned long long)fire_ts, cfg.sym_name.c_str(), cfg.parent_sz, midf,
          bv, buy_leg.filled, buy_leg.n_fills,
          (unsigned long long)(buy_leg.ended - buy_leg.started),
          mids, sv, sel_leg.filled, sel_leg.n_fills,
          (unsigned long long)(sel_leg.ended - sel_leg.started),
          slip_buy, slip_sel, slip_paired, slip_legsum,
          buy_leg.mkt_vol, sel_leg.mkt_vol,
          buy_leg.participation(), sel_leg.participation(),
          bmv, smv, slip_buy_vwap, slip_sel_vwap, slip_vs_vwap,
          afire, bsell, slip_buy_touch, slip_sel_touch, slip_vs_touch, outcome);
  fflush(out);
}

void SlippageProbe::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  if (!enabled()) return;

  // A fire still in flight at the end of the session is reported, not dropped.
  if (phase != Phase::WAITING)
    finish_fire(last_tim, "session_end");

  log_opr("probe summary: %d complete, %d partial, %d skipped, %zu scheduled",
          fires_done, fires_partial, fires_skipped, cfg.fire_ts.size());
  fprintf(stderr,
          "SlippageProbe %s: %d complete, %d partial, %d skipped of %zu scheduled; "
          "ticks=%llu eob=%llu last_tx=%llu bid=%d ask=%d next_fire_idx=%zu\n",
          cfg.sym_name.c_str(), fires_done, fires_partial, fires_skipped,
          cfg.fire_ts.size(), (unsigned long long)n_ticks, (unsigned long long)n_eob,
          (unsigned long long)last_tim, last_bid, last_ask, next_fire);

  if (out) { fclose(out); out = nullptr; }
}

}  // namespace sim
