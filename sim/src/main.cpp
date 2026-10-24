/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include "chutil/sig_hand.hpp"
#include "sim/SimKaspr.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fenv.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace po = boost::program_options;
using namespace std;


// ET wall-clock -> epoch ns, through the host tz database.
//
// Not chutil::Time: it formats with gmtime_r, so every wall-clock accessor on
// it is UTC. A probe schedule expressed as a fixed UTC hour would silently
// shift an hour against the exchange when DST starts on 2025-03-09, which is
// inside the test window. mktime() with TZ set does the real conversion.
static uint64_t et_to_epoch_ns(int y, int mo, int d, int h, int mi)
{
  char *old_tz = getenv("TZ");
  std::string saved = old_tz ? old_tz : "";
  setenv("TZ", "America/New_York", 1);
  tzset();

  struct tm tm {};
  tm.tm_year = y - 1900;
  tm.tm_mon  = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min  = mi;
  tm.tm_isdst = -1;            // let the tz database decide
  const time_t t = mktime(&tm);

  if (old_tz) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
  tzset();

  return uint64_t(t) * 1000000000ull;
}

// 09:29 to 16:00 ET inclusive, every `every_min` minutes.
static std::vector<uint64_t> probe_schedule(const std::string &date_yyyymmdd, int every_min,
                                            int end_hhmm = 1600)
{
  std::vector<uint64_t> out;
  if (date_yyyymmdd.size() != 8) return out;
  // `mins += every_min` never advances at 0 and runs backwards below it, so the
  // loop pushes until the process is killed by the OOM killer.
  if (every_min <= 0)
  {
    std::cerr << "--probe-every-min must be positive, got " << every_min << "\n";
    return out;
  }
  const int y  = std::stoi(date_yyyymmdd.substr(0, 4));
  const int mo = std::stoi(date_yyyymmdd.substr(4, 2));
  const int d  = std::stoi(date_yyyymmdd.substr(6, 2));
  // 09:29 to 16:00 ET. These are the window BOUNDARIES, not fire times: the
  // probe quotes continuously between them and each boundary closes one window
  // and opens the next, so N boundaries give N-1 measured windows.
  //
  // 16:00, not 15:30: the cash session runs to 16:00 and stopping half an hour
  // early threw away the close, which is a sixth of the day and not a typical
  // sixth -- volume and spread both move into it. It also cost the largest
  // parents the windows they most need: a 200-lot leg takes tens of seconds, so
  // Q100 and Q200 get few enough windows per session that half an hour matters.
  //
  // 09:29, not 09:30: a boundary exactly at the open puts the opening auction
  // and the first prints on the EDGE of a window, splitting them across two.
  // One minute earlier puts the open INSIDE the first window, which is where it
  // belongs -- the first window then spans 09:29-09:39 and contains the whole
  // of it.
  // --probe-end moves the 16:00 end; Treasury runs end at 12:00
  // (--probe-end 1200).
  const int end_mins = (end_hhmm / 100) * 60 + end_hhmm % 100;
  for (int mins = 9 * 60 + 29; mins <= end_mins; mins += every_min)
    out.push_back(et_to_epoch_ns(y, mo, d, mins / 60, mins % 60));
  return out;
}

// The trading days named by --probe-date: "YYYYMMDD" is that one day;
// "YYYYMMDD-YYYYMMDD" is every weekday (Mon-Fri) from the first to the last,
// inclusive. Empty on a malformed spec. Holidays are not removed: a day with no
// session simply produces no windows.
static std::vector<std::string> probe_dates(const std::string &spec)
{
  namespace ch = std::chrono;
  auto parse = [](const std::string &d, ch::year_month_day &out) {
    if (d.size() != 8 || !std::all_of(d.begin(), d.end(), ::isdigit)) return false;
    out = ch::year{std::stoi(d.substr(0, 4))} / std::stoi(d.substr(4, 2)) / std::stoi(d.substr(6, 2));
    return out.ok();
  };
  std::vector<std::string> out;
  const auto dash = spec.find('-');
  if (dash == std::string::npos)
  {
    ch::year_month_day one;
    if (parse(spec, one)) out.push_back(spec);
    return out;
  }
  ch::year_month_day first, last;
  if (!parse(spec.substr(0, dash), first) || !parse(spec.substr(dash + 1), last)) return out;
  for (ch::sys_days d = first; d <= ch::sys_days{last}; d += ch::days{1})
  {
    const ch::weekday wd{d};
    if (wd == ch::Saturday || wd == ch::Sunday) continue;
    const ch::year_month_day ymd{d};
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%04d%02u%02u", int(ymd.year()), unsigned(ymd.month()),
                  unsigned(ymd.day()));
    out.push_back(buf);
  }
  return out;
}

// Pull the session date out of a name like 310.20250115.databento.bin, so the
// common case needs no extra flag.
static std::string date_from_datafile(const std::string &path)
{
  size_t slash = path.find_last_of('/');
  std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
  for (size_t i = 0; i + 8 <= base.size(); ++i)
  {
    if (std::all_of(base.begin() + i, base.begin() + i + 8, ::isdigit) &&
        (i == 0 || !isdigit(base[i - 1])) &&
        (i + 8 == base.size() || !isdigit(base[i + 8])))
    {
      std::string cand = base.substr(i, 8);
      if (cand.substr(0, 2) == "20") return cand;
    }
  }
  return "";
}

int main(int argc, char* argv[])
{
  SET_ARGS;
  PRINT_ARGS;
  tzset();

  ::signal(SIGSEGV, &my_signal_handler);
  ::signal(SIGABRT, &my_signal_handler);
  ::signal(SIGBUS,  &my_signal_handler);
  ::signal(SIGFPE,  &my_signal_handler);
  ::signal(SIGPIPE, SIG_IGN);

  cout << "starting sim" << endl;
  cout << argv[0] << endl;
  cout << boost::filesystem::current_path() << endl;
  cout << "pid: " << getpid() << endl;


  po::options_description desc("Allowed options");
  desc.add_options()
      ("help",      "produce help message")
      ("datafile",  po::value<string>(), "gzip'd L3 .bin to replay (required)")
      ("universe",  po::value<string>(), "universe JSON for the session (required) -- "
                                         "see dbento_pcap_parse/scripts/build_universe_day.sh")
      ("contract",  po::value<string>(),
                    "contract to trade, e.g. ESH5 (required). The sim builds one book, "
                    "for this contract only.")
      // config_a, not config: sim/config/ was deleted when the arms were split
      // into config_a / config_b / config_close, but the default still named it.
      // SimKaspr reads the directory with read_info(config_dir_ + "/som.ini"),
      // which THROWS on a missing file -- so the sim did not fall back or warn,
      // it failed outright for anyone who did not pass --config.
      ("config",    po::value<string>()->default_value("config_a"),
                    "directory holding som.ini and lights.ini; relative paths "
                    "resolve against the working directory, so run from sim/ or "
                    "pass an absolute path")
      ("venue",     po::value<string>()->default_value("CMEMD"), "CMEMD or CMEMDFUT")
      ("ob-debug",  po::value<uint64_t>()->default_value(0),
                    "arm OB book tracing from this epoch-ns (0 = off). Verbose: "
                    "traces every add/mod and runs check_bbbo per order.")
      ("end-ts",    po::value<uint64_t>()->default_value(0),
                    "stop the replay once transactTime passes this epoch-ns "
                    "(0 = whole file). Use it to end a session at a wall-clock ET "
                    "time; compute per date so DST is handled.")
      ("place-rate-bp", po::value<int>()->default_value(-1),
                    "shadow placement rate in basis points of EOB ADDs "
                    "(e.g. 50=0.5%, 100=1%, 300=3%, 500=5%); -1 = use lights.ini")
      ("rng-seed",  po::value<uint32_t>()->default_value(0),
                    "deterministic seed for light placement; 0 = use lights.ini")
      ("ord-sz",    po::value<int>()->default_value(-1),
                    "per-placement child size; -1 = use lights.ini")
      ("ob-delay-us", po::value<int>()->default_value(-1),
                    "modelled one-way wire latency to the matching engine, in "
                    "microseconds. OB holds each of our orders on its delay "
                    "queue until ts0 + --ob-feed-delay-us + this has passed in "
                    "MARKET time, so it decides how much real flow gets in "
                    "front of us. The feed term is there because ts0 is the "
                    "market time the light SAW, already that old. Floor is "
                    "40us. -1 = OB's own default (1000us).")
      ("ob-cancel-delay-us", po::value<int>()->default_value(-1),
                    "modelled wire latency for CANCELS, microseconds. A cancel "
                    "goes over the same wire as an order, so -1 (= --ob-delay-us) "
                    "is the right default; set it only to test the asymmetric "
                    "case. Must be >= --ob-delay-us: a cancel is never faster "
                    "than a new order, since the matching engine has to locate "
                    "the resting order before it can pull it.")
      ("ob-feed-delay-us", po::value<int>()->default_value(0),
                    "modelled INBOUND feed latency, exchange -> us, in "
                    "microseconds. OB holds each market-data publish until "
                    "event_ts + this has passed in MARKET time, so the light "
                    "acts on STALE data rather than merely acting late. Without "
                    "it the model is asymmetric: the light saw the book "
                    "instantly and acted at --ob-delay-us, so the gap between "
                    "its place and its cancel was set by events it saw with no "
                    "delay and survived the latency intact. Raising "
                    "--ob-delay-us is NOT a substitute -- that makes the light "
                    "act later on FRESH data. 0 (the default) publishes "
                    "immediately and is identical to the path that existed "
                    "before. The order round trip becomes feed + order.")
      ("probe-size", po::value<int>()->default_value(0),
                    "SlippageProbe size in contracts per leg; 0 = no probe. The "
                    "probe quotes BOTH sides continuously from 09:29 to 16:00 ET "
                    "(BUY lights target +size, SEL lights -size) and measures each "
                    "leg's VWAP against the mid, the touch and the interval VWAP. "
                    "Nothing sells back to flat: inventory is carried and reported "
                    "as pos_at_close.")
      ("probe-out", po::value<string>()->default_value(""),
                    "CSV for the probe's per-fire rows; empty = log only")
      ("probe-date", po::value<string>()->default_value(""),
                    "trading day(s) for the probe: YYYYMMDD for one day, or "
                    "YYYYMMDD-YYYYMMDD for every weekday (Mon-Fri) in the range, "
                    "e.g. 20261019-20261023 for a week-long recording. Empty = the "
                    "date in the datafile name. ET wall-clock, DST handled.")
      ("probe-end", po::value<int>()->default_value(1600),
                    "end of the probe's trading day, HHMM ET (default 1600). "
                    "Use 1200 for Treasury futures.")
      ("probe-every-min", po::value<int>()->default_value(10),
                    "minutes between window boundaries (09:29-16:00 ET). This is "
                    "the MINIMUM window length: a window closes once it has "
                    "elapsed AND both legs have filled, so a slow leg gives a "
                    "longer window rather than a partial row.")
      ("nlights-per-side", po::value<int>()->default_value(-1),

                    "Lights per side per instrument (-1 = lights.ini, which\ndefaults to 4, matching production). A light holds ONE order at one\nprice, so this is what decides how many prices the shadow can rest at\nsimultaneously -- nlevels and max_dist cannot substitute for it.")
      ("max-dist",  po::value<int>()->default_value(-1),
                    "how deep the light rests, in ticks from the touch. Bounds "
                    "working size at (max_dist+1) x lev_orders_max, so at a "
                    "large parent this -- not the size -- can be what sets the "
                    "cost. -1 = use lights.ini (4).")
      ("quiet",     "log only errors, warnings and operator lines. A session "
                    "writes ~600MB of INFO otherwise, which bounds a sweep by "
                    "disk rather than by CPU.")
      ("logdebug",  "enable debug logs");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help") || argc == 1) {
    cout << desc << "\n\nExample:\n"
         << "  ./sim --datafile 310.20250115.databento.bin \\\n"
         << "        --universe universe.310.20250115.json \\\n"
         << "        --contract ESH5 --config config_a\n";
    return 1;
  }
  if (!vm.count("datafile")) { cerr << "Error: --datafile required\n"; return 1; }
  if (!vm.count("universe")) { cerr << "Error: --universe required\n"; return 1; }
  if (!vm.count("contract") || vm["contract"].as<string>().empty())
  { cerr << "Error: --contract required\n"; return 1; }

  const auto venue_str = vm["venue"].as<string>();
  const en::x venue = (venue_str == "CMEMDFUT") ? en::x::CMEMDFUT : en::x::CMEMD;

  polonaise::logger::act::Logger::rt = true;
  polonaise::logger::act::Logger::log_debug  = vm.count("logdebug") > 0;
  polonaise::logger::act::Logger::quiet      = vm.count("quiet") > 0;
  polonaise::logger::act::Logger::synchrolog = vm.count("logdebug") > 0;

  const int probe_size = vm["probe-size"].as<int>();
  std::vector<std::vector<uint64_t>> probe_days;   // window boundaries per trading day
  if (probe_size > 0)
  {
    std::string pdate = vm["probe-date"].as<string>();
    if (pdate.empty()) pdate = date_from_datafile(vm["datafile"].as<string>());
    if (pdate.empty())
    {
      cerr << "Error: --probe-size given but no --probe-date and none found in the "
              "datafile name\n";
      return 1;
    }
    const auto dates = probe_dates(pdate);
    if (dates.empty())
    {
      cerr << "Error: bad --probe-date '" << pdate << "': want YYYYMMDD or YYYYMMDD-YYYYMMDD "
              "with at least one weekday\n";
      return 1;
    }
    for (const auto &d : dates)
    {
      const int pend = vm["probe-end"].as<int>();
      if (pend < 1000 || pend > 2359 || pend % 100 > 59)
      {
        cerr << "Error: --probe-end must be HHMM between 1000 and 2359, got " << pend << "\n";
        return 1;
      }
      auto fires = probe_schedule(d, vm["probe-every-min"].as<int>(), pend);
      if (fires.empty())
      {
        cerr << "Error: could not build a probe schedule for date " << d << "\n";
        return 1;
      }
      // BOUNDARIES, not fires. N boundaries close N-1 windows: the first opens
      // the first window and each later one closes a window and opens the next.
      cerr << "probe schedule: " << fires.size() << " boundaries ("
           << (fires.size() - 1) << " windows) on " << d
           << " ET, first=" << fires.front() << " last=" << fires.back() << "\n";
      probe_days.push_back(std::move(fires));
    }
  }

  sim::SimKaspr* mgr = nullptr;
  try {
    mgr = new sim::SimKaspr(vm["datafile"].as<string>(),
                            vm["universe"].as<string>(),
                            vm["contract"].as<string>(),
                            vm["config"].as<string>(),
                            venue,
                            vm["ob-debug"].as<uint64_t>(),
                            vm["end-ts"].as<uint64_t>(),
                            vm["place-rate-bp"].as<int>(),
                            vm["rng-seed"].as<uint32_t>(),
                            vm["ord-sz"].as<int>(),
                            vm["ob-delay-us"].as<int>(),
                            vm["ob-cancel-delay-us"].as<int>(),
                            vm["ob-feed-delay-us"].as<int>(),
                            vm["max-dist"].as<int>(),
                            vm["nlights-per-side"].as<int>(),
                            probe_size,
                            vm["probe-out"].as<string>(),
                            probe_days);
  } catch (const std::exception& e) {
    cerr << "ERROR constructing SimKaspr: " << e.what() << "\n";
    return 1;
  }

#ifndef __APPLE__
  feenableexcept(FE_DIVBYZERO | FE_OVERFLOW | FE_INVALID);
#endif

  auto thrd = boost::thread(boost::ref(*mgr));
  thrd.join();

  cout << "=== sim complete ===" << endl;
  return 0;
}
