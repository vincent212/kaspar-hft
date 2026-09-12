/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * ordtrace — trace individual orders through a .bin, and audit "stale" orders.
 *
 * Two modes:
 *
 *   1. Trace: --orderid N (repeatable) lists every MBO event for those order
 *      ids, in file order, with timestamps and the decoded action.
 *
 *   2. Stale audit: --secid S --at T rebuilds the book from the raw MBO stream
 *      up to time T, lists what is still resting on the chosen side within a
 *      price window, and then scans the REST of the file for each of those
 *      orders to answer the question that matters: does the capture actually
 *      contain a cancel for it?
 *
 * Mode 2 is the one that distinguishes "our book missed a cancel" from "the
 * capture never carried one".
 *
 * Prices: pxd is in CME native units. ticks = pxd / minPriceIncrement
 * (25 for ES). Sides in the MBO stream are '0' = bid, '1' = ask.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include "chutil/Macros.hpp"
#include "bfile/r_l3.hpp"

namespace po = boost::program_options;

namespace {

constexpr char SIDE_BID = '0';
constexpr char SIDE_ASK = '1';

const char* action_name(int a)
{
  switch (a) {
    case 0:  return "NEW";
    case 1:  return "CHANGE";
    case 2:  return "DELETE";
    case 3:  return "DELETE_THRU";
    case 4:  return "DELETE_FROM";
    case 5:  return "OVERLAY";
    default: return "?";
  }
}

std::string tfmt(uint64_t ns)
{
  const time_t s = static_cast<time_t>(ns / 1000000000ULL);
  struct tm tv;
  gmtime_r(&s, &tv);
  char b[32];
  strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tv);
  char out[64];
  snprintf(out, sizeof out, "%s.%09llu", b, static_cast<unsigned long long>(ns % 1000000000ULL));
  return out;
}

struct Event {
  uint64_t t;
  int      action;
  double   pxd;
  uint32_t qty;
  char     side;
  uint64_t prio;
  int32_t  sec;
  // OB gates records on the recovery flag, so a delete that carries it can be
  // dropped while the add that preceded it was applied -- which leaves
  // liquidity in the book that the exchange has removed.
  bool     recovery;
  bool     eoe;
};

struct Resting {
  double   pxd;
  uint32_t qty;
  char     side;
  uint64_t added;     // when it first appeared
  uint64_t last_upd;  // last NEW/CHANGE
  uint64_t prio;
};

gzFile must_open(const std::string& path)
{
  gzFile f = gzopen(path.c_str(), "rb");
  if (!f) ERRF(boost::format("cannot open %s") % path);
  return f;
}

// Every MBO event for a set of order ids, whole file.
std::map<uint64_t, std::vector<Event>>
collect_events(const std::string& file, const std::set<uint64_t>& oids, int32_t sec_filter)
{
  std::map<uint64_t, std::vector<Event>> out;
  gzFile f = must_open(file);
  bfile::l3_t l3;
  uint64_t ts = 0;
  while (bfile::read_l3(f, l3, ts)) {
    if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) {
      const auto& m = std::get<bfile::l3_mbo_v2_t>(l3);
      if (sec_filter && m.securityID != sec_filter) continue;
      if (!oids.count(m.orderID)) continue;
      out[m.orderID].push_back({m.transactTime, m.orderUpdateAction, m.pxd,
                                m.displayQty, m.side, m.priority, m.securityID,
                                m.recovery, m.endOfEvent});
    }
  }
  gzclose(f);
  return out;
}

} // namespace

int main(int argc, char* argv[])
{
  SET_ARGS;

  po::options_description desc("ordtrace options");
  desc.add_options()
      ("help",     "produce help message")
      ("datafile", po::value<std::string>(), "gzip'd L3 .bin to scan (required)")
      ("orderid",  po::value<std::vector<uint64_t>>()->multitoken(),
                   "trace these order ids (repeatable)")
      ("secid",    po::value<int32_t>()->default_value(0), "restrict to this securityID")
      ("at",       po::value<uint64_t>()->default_value(0),
                   "stale audit: rebuild the book up to this transactTime (ns)")
      ("side",     po::value<std::string>()->default_value("ask"),
                   "stale audit: which side to examine (ask|bid)")
      ("px-max",   po::value<int>()->default_value(0),
                   "stale audit: only orders at or below this tick (ask side)")
      ("px-min",   po::value<int>()->default_value(0),
                   "stale audit: only orders at or above this tick (bid side)")
      ("units",    po::value<double>()->default_value(25.0),
                   "minPriceIncrement in native units; ticks = pxd / units")
      ("limit",    po::value<int>()->default_value(40), "max orders to report")
      ("status",   po::bool_switch(),
                   "status mode: dump security-status records around --at. A "
                   "crossed book is legal when the instrument is not matching "
                   "-- pre-open, halt, or a velocity-logic reserve -- so this "
                   "is what separates our bug from the exchange's own state.")
      ("window-s", po::value<double>()->default_value(60.0),
                   "status mode: seconds either side of --at")
      ("actions",  po::bool_switch(),
                   "action mode: histogram of orderUpdateAction. MDP3 defines "
                   "3 DELETE_THRU, 4 DELETE_FROM and 5 OVERLAY as well as the "
                   "usual 0/1/2, and a book that ignores those keeps liquidity "
                   "the exchange has removed.");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help") || argc == 1) {
    std::cout << desc << "\n"
              << "Examples:\n"
              << "  # every event for one order\n"
              << "  ordtrace --datafile 310.20250110.databento.bin --orderid 6414296120517\n\n"
              << "  # what was still resting on the ask at/below tick 23724 at the cross,\n"
              << "  # and does the capture contain a cancel for each?\n"
              << "  ordtrace --datafile 310.20250110.databento.bin --secid 5002 \\\n"
              << "           --at 1736467515805034641 --side ask --px-max 23724\n";
    return 1;
  }
  if (!vm.count("datafile")) { std::cerr << "Error: --datafile required\n"; return 1; }

  const auto file  = vm["datafile"].as<std::string>();
  const auto sec   = vm["secid"].as<int32_t>();
  const auto units = vm["units"].as<double>();
  const auto limit = vm["limit"].as<int>();

  // ---- mode 1: trace explicit order ids -------------------------------
  if (vm.count("orderid")) {
    std::set<uint64_t> oids;
    for (auto id : vm["orderid"].as<std::vector<uint64_t>>()) oids.insert(id);
    auto ev = collect_events(file, oids, sec);
    for (auto id : oids) {
      auto it = ev.find(id);
      std::cout << "=== order " << id << " : "
                << (it == ev.end() ? 0 : it->second.size()) << " events ===\n";
      if (it == ev.end()) { std::cout << "  NOT PRESENT in this file\n"; continue; }
      for (const auto& e : it->second)
        std::cout << "  " << tfmt(e.t) << "Z  " << action_name(e.action)
                  << " side=" << (e.side == SIDE_ASK ? "ask" : "bid")
                  << " px=" << e.pxd << " (" << int(e.pxd / units + 0.1) << "t)"
                  << " qty=" << e.qty << " prio=" << e.prio
                  << " secID=" << e.sec
                  << (e.recovery ? "  RECOVERY" : "")
                  << (e.eoe ? " eoe" : "") << "\n";
    }
    return 0;
  }

  // ---- mode 1a: orderUpdateAction histogram ----------------------------
  if (vm["actions"].as<bool>())
  {
    const int32_t sec = vm["secid"].as<int32_t>();
    gzFile f = must_open(vm["datafile"].as<std::string>());
    bfile::l3_t l3;
    uint64_t ts = 0;
    long long counts[256] = {0};
    long long total = 0;
    // OB drops any non-recovery record at or before the newest recovery record
    // it has seen, so the presence and time span of these decides whether that
    // gate can silently eat live data.
    long long recov = 0;
    uint64_t recov_lo = 0, recov_hi = 0;
    while (bfile::read_l3(f, l3, ts)) {
      if (!std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) continue;
      const auto& m = std::get<bfile::l3_mbo_v2_t>(l3);
      if (sec && m.securityID != sec) continue;
      ++counts[m.orderUpdateAction & 0xff];
      ++total;
      if (m.recovery) {
        ++recov;
        if (!recov_lo || m.transactTime < recov_lo) recov_lo = m.transactTime;
        if (m.transactTime > recov_hi) recov_hi = m.transactTime;
      }
    }
    gzclose(f);
    std::printf("orderUpdateAction histogram for secID %d (%lld MBO records)\n\n",
                sec, total);
    for (int a = 0; a < 256; a++)
      if (counts[a])
        std::printf("  %3d %-12s %12lld  %6.3f%%\n", a, action_name(a), counts[a],
                    total ? 100.0 * double(counts[a]) / double(total) : 0.0);
    std::printf("\n  recovery-flagged: %lld", recov);
    if (recov) std::printf("  spanning %s .. %s", tfmt(recov_lo).c_str(),
                           tfmt(recov_hi).c_str());
    std::printf("\n");
    return 0;
  }

  // ---- mode 1b: security status around a time --------------------------
  if (vm["status"].as<bool>())
  {
    const uint64_t at  = vm["at"].as<uint64_t>();
    const int32_t  sec = vm["secid"].as<int32_t>();
    if (!at) { std::cerr << "Error: --status needs --at\n"; return 1; }
    const auto win = uint64_t(vm["window-s"].as<double>() * 1e9);
    const uint64_t lo = at > win ? at - win : 0;
    const uint64_t hi = at + win;

    gzFile f = must_open(vm["datafile"].as<std::string>());
    bfile::l3_t l3;
    uint64_t ts = 0;
    int n = 0;
    std::printf("security status for secID %d, %.1fs either side of %llu\n\n",
                sec, vm["window-s"].as<double>(), (unsigned long long)at);
    while (bfile::read_l3(f, l3, ts)) {
      if (!std::holds_alternative<bfile::l3_sst_t>(l3)) continue;
      const auto& r = std::get<bfile::l3_sst_t>(l3);
      if (sec && r.securityID != sec) continue;
      if (r.txtim < lo || r.txtim > hi) continue;
      const double rel = (double(r.txtim) - double(at)) / 1e9;
      std::printf("%s  %+9.4fs  secID=%d tradingStatus=%u haltReason=%u tradingEvent=%u\n",
                  tfmt(r.txtim).c_str(), rel, r.securityID,
                  unsigned(r.tradingStatus), unsigned(r.haltReason),
                  unsigned(r.tradingEvent));
      ++n;
    }
    gzclose(f);
    if (!n) std::printf("(no security-status records in the window)\n");
    return 0;
  }

  // ---- mode 2: stale audit --------------------------------------------
  const auto at = vm["at"].as<uint64_t>();
  if (!at) { std::cerr << "Error: --at required (or use --orderid)\n"; return 1; }
  if (!sec) { std::cerr << "Error: --secid required for the stale audit\n"; return 1; }

  const bool want_ask = vm["side"].as<std::string>() != "bid";
  const int  px_max   = vm["px-max"].as<int>();
  const int  px_min   = vm["px-min"].as<int>();

  // Pass 1: rebuild the book from raw MBO up to `at`.
  std::map<uint64_t, Resting> book;
  {
    gzFile f = must_open(file);
    bfile::l3_t l3;
    uint64_t ts = 0;
    long n = 0;
    while (bfile::read_l3(f, l3, ts)) {
      if (!std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) continue;
      const auto& m = std::get<bfile::l3_mbo_v2_t>(l3);
      if (m.securityID != sec) continue;
      if (m.transactTime > at) break;
      ++n;
      if (m.orderUpdateAction == 0 || m.orderUpdateAction == 1) {
        auto it = book.find(m.orderID);
        if (it == book.end())
          book[m.orderID] = {m.pxd, m.displayQty, m.side, m.transactTime, m.transactTime, m.priority};
        else { it->second.pxd = m.pxd; it->second.qty = m.displayQty;
               it->second.last_upd = m.transactTime; it->second.prio = m.priority; }
      } else {
        book.erase(m.orderID);
      }
    }
    gzclose(f);
    std::cout << "replayed " << n << " MBO events for secID " << sec
              << " up to " << tfmt(at) << "Z\n"
              << "resting orders in the TRUE book: " << book.size() << "\n";
  }

  // True top of book, for context.
  {
    int tb = -1, ta = INT32_MAX;
    for (const auto& kv : book) {
      const int t = int(kv.second.pxd / units + 0.1);
      if (kv.second.side == SIDE_ASK) ta = std::min(ta, t);
      else                            tb = std::max(tb, t);
    }
    std::cout << "TRUE best_bid=" << tb << "t  best_ask=" << ta
              << "t  spread=" << (ta - tb) << "t\n\n";
  }

  // Candidates: resting on the chosen side inside the price window.
  std::set<uint64_t> candidates;
  for (const auto& kv : book) {
    const auto& r = kv.second;
    if (want_ask != (r.side == SIDE_ASK)) continue;
    const int t = int(r.pxd / units + 0.1);
    if (want_ask && px_max && t > px_max) continue;
    if (!want_ask && px_min && t < px_min) continue;
    candidates.insert(kv.first);
  }

  std::cout << "=== orders still resting on the "
            << (want_ask ? "ASK" : "BID") << " side inside the window: "
            << candidates.size() << " ===\n";
  if (candidates.empty()) {
    std::cout << "  (none) -- the TRUE book is not crossed here; any cross is our own\n"
                 "  stale state, not something the capture contains.\n";
    return 0;
  }

  // Pass 2: for each candidate, does the capture contain a LATER cancel?
  auto ev = collect_events(file, candidates, sec);
  int shown = 0, with_cancel = 0, without = 0;
  for (auto id : candidates) {
    const auto& r = book.at(id);
    const auto& evs = ev[id];
    uint64_t cancel_t = 0;
    for (const auto& e : evs)
      if (e.t > at && e.action >= 2) { cancel_t = e.t; break; }

    if (cancel_t) ++with_cancel; else ++without;
    if (shown++ < limit) {
      std::cout << "oid=" << id << " px=" << int(r.pxd / units + 0.1) << "t qty=" << r.qty
                << " added=" << tfmt(r.added) << "Z age="
                << double(at - r.added) / 1e9 << "s events=" << evs.size() << "\n";
      if (cancel_t)
        std::cout << "    CANCEL PRESENT in capture at " << tfmt(cancel_t)
                  << "Z (+" << double(cancel_t - at) / 1e9 << "s after the cross)\n";
      else
        std::cout << "    NO CANCEL anywhere in the capture after this point\n";
    }
  }
  std::cout << "\nsummary: " << with_cancel << " have a later cancel in the capture, "
            << without << " do not\n";
  return 0;
}
