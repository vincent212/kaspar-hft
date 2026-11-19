/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/Instruments.hpp"

#include <chrono>
#include <iostream>
#include <numeric>

#include "frame/ref/RefData.hpp"

namespace jnx_md
{
  const char *to_string(Phase p)
  {
    switch (p)
    {
    case Phase::Pre: return "pre";
    case Phase::Open: return "open";
    case Phase::Closed: return "closed";
    default: return "unknown";
    }
  }

  uint64_t jst_midnight_ns(int y, int m, int d)
  {
    using namespace std::chrono;
    const sys_days dd{year{y} / month{unsigned(m)} / day{unsigned(d)}};
    const int64_t s = int64_t(dd.time_since_epoch().count()) * 86400 - 9 * 3600;
    return uint64_t(s) * 1000000000ull;
  }

  uint64_t jst_midnight_ns(int yyyymmdd)
  {
    return jst_midnight_ns(yyyymmdd / 10000, yyyymmdd / 100 % 100, yyyymmdd % 100);
  }

  int session_date(uint64_t utc_ns)
  {
    using namespace std::chrono;
    // JST is UTC+9; the session date is that of 6 hours earlier.
    const int64_t s = int64_t(utc_ns / 1000000000ull) + 9 * 3600 - 6 * 3600;
    const sys_days d{days{s >= 0 ? s / 86400 : (s - 86399) / 86400}};
    const year_month_day ymd{d};
    return int(ymd.year()) * 10000 + int(unsigned(ymd.month())) * 100 + int(unsigned(ymd.day()));
  }

  // Market Events Schedule 1.2: J, X and U markets trade 08:20 to 16:30; the
  // night market 17:00 to 30:00 (06:00 the next day). Seconds are counted from
  // midnight of the session date, so the night session runs past 86,400.
  Phase clock_phase(const std::string &group, uint64_t secs)
  {
    uint64_t open = 8 * 3600 + 20 * 60, close = 16 * 3600 + 30 * 60;
    if (group == "NGHT")
    {
      open = 17 * 3600;
      close = 30 * 3600;
    }
    if (secs < open)
      return Phase::Pre;
    if (secs < close)
      return Phase::Open;
    return Phase::Closed;
  }

  void Instruments::tick_row(uint32_t table, uint32_t tick, uint32_t start)
  {
    tables_[table][start] = tick;
  }

  uint32_t Instruments::tick_at(uint32_t table, uint32_t px) const
  {
    auto t = tables_.find(table);
    if (t == tables_.end() || t->second.empty())
      return 0;
    const auto &rows = t->second;
    auto it = rows.lower_bound(px);   // first start >= px
    if (it == rows.begin())
      return it->second;              // px at or below the first start
    return std::prev(it)->second;     // highest start below px
  }

  uint32_t Instruments::unit_for(uint32_t table, uint32_t upper, uint32_t lower) const
  {
    auto t = tables_.find(table);
    if (t == tables_.end() || t->second.empty())
      return 1;
    const auto &rows = t->second;
    const bool limits = upper > 0 && upper >= lower;
    uint32_t g = 0;
    for (auto it = rows.begin(); it != rows.end(); ++it)
    {
      auto next = std::next(it);
      // Band (start, next start]; both boundary readings are included.
      const bool reaches = !limits ||
                           (it->first <= upper && (next == rows.end() || next->first >= lower));
      if (reaches)
        g = std::gcd(g, it->second);
    }
    return g ? g : 1;
  }

  const std::vector<Instrument *> &Instruments::group(const std::string &g) const
  {
    static const std::vector<Instrument *> none;
    auto it = groups_.find(g);
    return it == groups_.end() ? none : it->second;
  }

  Instrument *Instruments::directory(const std::string &sicc, const std::string &isin,
                                     const std::string &group, const BookKey &key,
                                     uint32_t round_lot, uint32_t table, uint32_t decimals,
                                     uint32_t upper, uint32_t lower)
  {
    Instrument *i = find(key);
    const bool fresh = !i;
    if (fresh)
    {
      all_.push_back(std::make_unique<Instrument>());
      i = all_.back().get();
      i->key = key;
      i->sicc = sicc;
      i->group = group;
      by_key_[key] = i;
      groups_[group].push_back(i);
    }
    i->isin = isin;
    i->round_lot = round_lot;
    i->table = table;
    i->decimals = decimals;
    i->upper = upper;
    i->lower = lower;
    i->listed = true;
    const uint32_t unit = unit_for(table, upper, lower);
    if (fresh)
    {
      i->unit = unit;
      register_asset(*i);
      by_asset_[i->asset] = i;
    }
    else if (unit != i->unit)
    {
      // OB was built with the old tick. Keep it when the old tick divides the
      // new one (every new price is still on the ladder); otherwise prices off
      // the old grid are counted as px_off_unit.
      ++unit_changes_;
      std::cerr << "jnx: " << sicc << "." << group << " tick changed from " << i->unit
                << " to " << unit << " venue units; keeping " << i->unit << std::endl;
    }
    return i;
  }

  void Instruments::register_asset(Instrument &i)
  {
    const uint32_t idx = uint32_t(all_.size() - 1);
    const uint64_t top = i.upper ? uint64_t(i.upper) / i.unit + 1 : (1u << 22);
    i.maxpx = int(std::min<uint64_t>(top + 1, 0x7FFFFFF0ull));
    if (!cfg_.register_assets)
    {
      i.asset = cfg_.secid_base + int32_t(idx);
      return;
    }
    namespace ref = frame::ref;
    ref::RefData::inst();   // add_future_asset does not construct RefData itself
    const std::string name = cfg_.name_prefix + i.sicc + "." + i.group;
    const double tick_jpy = double(i.unit) / 10.0;   // PriceDecimals is 1 (ITCH 4.4)
    auto a = const_cast<ref::Asset *>(ref::RefData::get_asset(name));
    if (!a)
      a = ref::RefData::add_future_asset(name, en::x::JNX, cfg_.secid_base + int32_t(idx),
                                         "", i.group, tick_jpy);
    else if (a->get_units() != tick_jpy)
      std::cerr << "jnx: asset " << name << " from the universe has tick " << a->get_units()
                << ", the directory gives " << tick_jpy << "; using the universe's" << std::endl;
    if (a->maxpx < i.maxpx)
      a->maxpx = i.maxpx;
    a->has_book = true;
    i.asset = int32_t(a->id);
  }

  void Instruments::reset()
  {
    tables_.clear();
    for (auto &i : all_)
    {
      i->listed = false;
      i->trading_state = 0;
      i->short_sell = '0';
      i->ref_price = 0;
      i->status_sent = 0;
      i->book.clear();
    }
  }
}
