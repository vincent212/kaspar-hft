/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

//
// obcmp -- run ONE instrument of a capture through OB and TachBook side by
// side and write what each publishes, so the two books can be compared and
// every trade print checked against the bid/offer (obcmp_report.py).
//
// Both books get every record exactly as the feed handler hands it to them
// (a frame::mda::msg::Data with the raw l3 record, israw=true): the
// instrument's MBO records, and the trade records of its orders. Records are
// fed from the start of the file so both books are built; output is written
// only inside [t0, t1).
//
// Each book's handlers are called synchronously instead of through actor
// threads, so the order is exactly the wire order and the run is
// deterministic.
//
// Output (CSV, in the current directory):
//   trades_tachbook.csv, trades_ob.csv  -- TradeNotify: txtim, px, resting side, qty, oid
//   levels_ref.csv                      -- the reference market: best bid/offer and
//                                          sizes from a minimal book built here from
//                                          the same MBO records, independent of both
//                                          books under test, after every record that
//                                          changes it while bid < offer
//   bbo_ob.csv                          -- OB BBBOChg (OB throttles these to 10/s)
//
// usage: obcmp <capture.bin> <securityID> <t0_ns> <t1_ns>
//
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <typeindex>
#include <unistd.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <zlib.h>
#include <boost/property_tree/ptree.hpp>
#include "bfile/r_l3.hpp"
#include "actors/msg/Start.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ob/act/TachBook.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include <cmath>

namespace
{
  uint64_t g_t0 = 0, g_t1 = UINT64_MAX;
  double g_tick = 0;

  bool in_window(uint64_t t) { return t >= g_t0 && t < g_t1; }

  bool invoke(actors::Actor *a, const actors::Message *m, actors::Actor *sender)
  {
    const_cast<actors::Message *>(m)->sender = sender;
    auto it = a->handlers.find(std::type_index(typeid(*m)));
    if (it == a->handlers.end())
      return false;
    (a->*(it->second))(m);
    return true;
  }

  // The reference market. Orders by ID; levels summed per side. A New for an
  // order already held (a snapshot re-sending it) replaces it; an order that
  // crosses the other side removes the orders it crosses.
  class RefBook
  {
  public:
    void apply(const bfile::l3_mbo_v2_t &m)
    {
      auto it = ord_.find(m.orderID);
      if (it != ord_.end())
      {
        level(it->second.buy, it->second.px) -= it->second.qty;
        prune(it->second.buy, it->second.px);
        ord_.erase(it);
      }
      if (m.orderUpdateAction == 2 || m.displayQty == 0 || m.pxd <= 0)
        return;
      if (m.side != '0' && m.side != '1')
        return;
      Ord o{m.side == '0', int64_t(std::llround(m.pxd / g_tick)), int64_t(m.displayQty)};
      level(o.buy, o.px) += o.qty;
      ord_.emplace(m.orderID, o);
      // Uncross, as TachBook's UNCROSS_BOOK does: the capture misses some
      // deletes, and an order left behind that this one crosses is stale.
      // A live aggressor's residual is only added after its fills, so it
      // never crosses a live order.
      const bool crossed = o.buy ? (!ask_.empty() && ask_.begin()->first <= o.px)
                                 : (!bid_.empty() && bid_.rbegin()->first >= o.px);
      if (crossed)
        for (auto i = ord_.begin(); i != ord_.end();)
        {
          const auto &x = i->second;
          if (x.buy != o.buy && (o.buy ? x.px <= o.px : x.px >= o.px))
          {
            level(x.buy, x.px) -= x.qty;
            prune(x.buy, x.px);
            i = ord_.erase(i);
          }
          else
            ++i;
        }
    }
    bool top(int64_t &b, int64_t &bs, int64_t &a, int64_t &as) const
    {
      if (bid_.empty() || ask_.empty())
        return false;
      b = bid_.rbegin()->first; bs = bid_.rbegin()->second;
      a = ask_.begin()->first;  as = ask_.begin()->second;
      return b < a;
    }

  private:
    struct Ord { bool buy; int64_t px, qty; };
    std::unordered_map<uint64_t, Ord> ord_;
    std::map<int64_t, int64_t> bid_, ask_;
    int64_t &level(bool buy, int64_t px) { return (buy ? bid_ : ask_)[px]; }
    void prune(bool buy, int64_t px)
    {
      auto &side = buy ? bid_ : ask_;
      auto it = side.find(px);
      if (it != side.end() && it->second <= 0)
        side.erase(it);
    }
  };

  // Receives what one book publishes and writes it out. The name admits it to
  // TachBook's HI-prio subscriber list (subscribe_handler's ACL).
  class Sink : public actors::Actor
  {
  public:
    Sink(const char *nm, const std::string &tag) : name_(nm)
    {
      trades_ = fopen(("trades_" + tag + ".csv").c_str(), "w");
      fprintf(trades_, "txtim,px,resting_side,qty,oid\n");
      if (tag == "ob")
      {
        bbo_ = fopen("bbo_ob.csv", "w");
        fprintf(bbo_, "txtim,bid_ticks,ask_ticks\n");
      }
    }
    const char *get_name() const override { return name_; }

    // The books allocate these with `new` (the framework deletes after
    // delivery); we are the framework here.
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      if (auto t = dynamic_cast<const frame::ob::msg::TradeNotify *>(m))
      {
        const auto &p = *t->payload;
        if (in_window(p.txtim_epoch))
          fprintf(trades_, "%lu,%.10Lg,%s,%d,%lu\n", (unsigned long)p.txtim_epoch, p.px.to_double(),
                  p.side == en::bs::BUY ? "BUY" : "SEL", int(p.sz), (unsigned long)p.ex_order_id);
        ++n_trades;
      }
      else if (auto b = dynamic_cast<const frame::ob::msg::BBBOChg *>(m))
      {
        if (bbo_ && in_window(b->tx_time))
          fprintf(bbo_, "%lu,%d,%d\n", (unsigned long)b->tx_time, b->best_bid, b->best_ask);
      }
      delete m;
    }

    uint64_t n_trades = 0;

    void close()
    {
      for (FILE **f : {&trades_, &bbo_})
        if (*f)
        {
          fclose(*f);
          *f = nullptr;
        }
    }

  private:
    const char *name_;
    FILE *trades_ = nullptr, *bbo_ = nullptr;
  };
} // namespace

int main(int argc, char **argv)
{
  if (argc != 5)
  {
    fprintf(stderr, "usage: obcmp <capture.bin> <securityID> <t0_ns> <t1_ns>\n");
    return 2;
  }
  const int32_t want = atoi(argv[2]);
  g_t0 = strtoull(argv[3], nullptr, 10);
  g_t1 = strtoull(argv[4], nullptr, 10);

  gzFile f = gzopen(argv[1], "rb");
  if (!f)
    return 1;

  Sink tb_sink("aggr_obcmp_tachbook", "tachbook"), ob_sink("aggr_obcmp_ob", "ob");
  frame::ob::act::TachBook *tb = nullptr;
  frame::ob::act::OB *ob = nullptr;
  boost::property_tree::ptree pt;   // OB reads nothing from an empty tree

  bfile::l3_t l3;
  uint64_t ts = 0, n_mbo = 0, n_trd = 0;
  std::unordered_set<uint64_t> our_orders;   // orderIDs ever added for this instrument
  RefBook ref;
  FILE *ref_out = fopen("levels_ref.csv", "w");
  fprintf(ref_out, "timestamp,bid,bid_sz,ask,ask_sz\n");
  int64_t pb = -1, pbs = -1, pa = -1, pas = -1;

  auto feed = [&](const bfile::l3_t &rec) {
    frame::mda::msg::Data d;
    d.l3 = rec;
    d.israw = true;
    invoke(tb, &d, nullptr);
    frame::mda::msg::Data d2;
    d2.l3 = rec;
    d2.israw = true;
    invoke(ob, &d2, nullptr);
  };

  while (bfile::read_l3(f, l3, ts))
  {
    if (std::holds_alternative<bfile::l3_fdf_t>(l3))
    {
      const auto &fdf = std::get<bfile::l3_fdf_t>(l3);
      if (fdf.securityID != want || tb)
        continue;
      std::string sym(fdf.sym, strnlen(fdf.sym, sizeof(fdf.sym)));
      std::string cfi(fdf.cfiCode, strnlen(fdf.cfiCode, sizeof(fdf.cfiCode)));
      std::string grp(fdf.securityGroup, strnlen(fdf.securityGroup, sizeof(fdf.securityGroup)));
      g_tick = fdf.minPriceIncrement * fdf.dispFactor;
      // CME sends a null price limit as ~9.2e9 (ZNZ6 2026-10-08); use a
      // 180-point ladder when the limit is not a real price.
      const bool real_limit = fdf.high_limit_px > 0.0 && fdf.high_limit_px < 1e6;
      const double range = real_limit ? fdf.high_limit_px * 3.0 : 180.0;
      (void)frame::ref::RefData::inst();
      auto a = frame::ref::RefData::add_future_asset(sym, en::x(fdf.venue), fdf.securityID, cfi, grp, g_tick);
      a->maxpx = int(range / g_tick);
      tb = new frame::ob::act::TachBook(a->id);
      ob = new frame::ob::act::OB(nullptr, false, nullptr, a->id, pt);
      invoke(tb, new actors::msg::Start(), nullptr);
      // TachBook: trades (HI-prio Subscribe).
      auto tsub = new frame::mda::msg::Subscribe();
      tsub->prio = frame::mda::msg::Subscribe::HI;
      invoke(tb, tsub, &tb_sink);
      // OB takes these in process_message (no registered handler), reached
      // through the framework's synchronous fast_send. OB's Start only looks
      // up the simulated order manager; not needed here.
      auto osub = new frame::mda::msg::Subscribe();
      osub->prio = frame::mda::msg::Subscribe::HI;
      ob->fast_send(osub, &ob_sink);
      ob->fast_send(new frame::ob::msg::BBBOSub(), &ob_sink);
      fprintf(stderr, "books created for %s secid %d tick %.10g asset id %d\n",
              sym.c_str(), want, g_tick, (int)a->id);
      continue;
    }
    if (!tb)
      continue;
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
    {
      const auto &m = std::get<bfile::l3_mbo_v2_t>(l3);
      if (m.securityID != want)
        continue;
      if (!m.recovery && m.transactTime >= g_t1 + 1'000'000'000ULL)
        break;   // a second past the window: everything inside it has been published
      if (m.orderUpdateAction == 0)
        our_orders.insert(m.orderID);
      ++n_mbo;
      feed(l3);
      ref.apply(m);
      int64_t b, bs, a, as;
      if (in_window(m.transactTime) && ref.top(b, bs, a, as) &&
          (b != pb || bs != pbs || a != pa || as != pas))
      {
        fprintf(ref_out, "%lu,%.10g,%ld,%.10g,%ld\n", (unsigned long)m.transactTime,
                b * g_tick, (long)bs, a * g_tick, (long)as);
        pb = b; pbs = bs; pa = a; pas = as;
      }
    }
    else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3))
    {
      const auto &t = std::get<bfile::l3_mbo_trd_v2_t>(l3);
      if (!our_orders.count(t.orderID))
        continue;   // orders no book knows; both books would ignore them
      ++n_trd;
      feed(l3);
    }
  }
  gzclose(f);
  fprintf(stderr, "fed %lu MBO and %lu trade records; TradeNotify: TachBook %lu, OB %lu\n",
          (unsigned long)n_mbo, (unsigned long)n_trd, (unsigned long)tb_sink.n_trades,
          (unsigned long)ob_sink.n_trades);
  tb_sink.close();
  ob_sink.close();
  fclose(ref_out);
  _exit(0);   // skip actor teardown (the books were never added to a running group)
}
