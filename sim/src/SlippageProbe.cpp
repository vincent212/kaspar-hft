/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
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
  buy_leg.ended   = now;
  mid_sell        = last_bid + last_ask;
  sel_leg.started = now;
  phase = Phase::SELLING;

  log_inf("fire %zu buy leg done: filled=%.0f vwap=%.2f -> selling back",
          next_fire, buy_leg.filled, buy_leg.vwap());
  set_target(0);
}

void SlippageProbe::finish_fire(uint64_t now, const char *why) noexcept
{
  sel_leg.ended = now;
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
  if (uint32_t(pld->point_.sym) != cfg.sym) return;

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

  log_inf("fill %s %.0f @ %d pos=%d (leg filled=%.0f vwap=%.2f)",
          en::to_string(m->side), sz, m->pxi, position, leg.filled, leg.vwap());
}

void SlippageProbe::emit_header()
{
  fprintf(out,
          "fire_ts,sym,parent_sz,mid_fire,buy_vwap,buy_filled,buy_fills,buy_ns,"
          "mid_sell,sel_vwap,sel_filled,sel_fills,sel_ns,"
          "slip_buy_ticks,slip_sel_ticks,slip_paired_ticks,outcome\n");
  fflush(out);
}

void SlippageProbe::emit_row(const char *outcome)
{
  // Ticks. mid_* are 2x, so halve them here and nowhere else.
  const double midf = mid_fire / 2.0;
  const double mids = mid_sell / 2.0;
  const double bv   = buy_leg.vwap();
  const double sv   = sel_leg.vwap();

  const double slip_buy    = buy_leg.filled > 0 ? bv - midf : 0.0;
  const double slip_sel    = sel_leg.filled > 0 ? mids - sv : 0.0;
  const double slip_paired = (buy_leg.filled > 0 && sel_leg.filled > 0)
                               ? (bv - sv) / 2.0 : 0.0;

  log_inf("FIRE DONE %s: buy %.0f@%.2f sel %.0f@%.2f "
          "slip_buy=%.3f slip_sel=%.3f slip_paired=%.3f",
          outcome, buy_leg.filled, bv, sel_leg.filled, sv,
          slip_buy, slip_sel, slip_paired);

  if (!out) return;
  fprintf(out,
          "%llu,%s,%d,%.2f,%.4f,%.0f,%d,%llu,"
          "%.2f,%.4f,%.0f,%d,%llu,"
          "%.4f,%.4f,%.4f,%s\n",
          (unsigned long long)fire_ts, cfg.sym_name.c_str(), cfg.parent_sz, midf,
          bv, buy_leg.filled, buy_leg.n_fills,
          (unsigned long long)(buy_leg.ended - buy_leg.started),
          mids, sv, sel_leg.filled, sel_leg.n_fills,
          (unsigned long long)(sel_leg.ended - sel_leg.started),
          slip_buy, slip_sel, slip_paired, outcome);
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
