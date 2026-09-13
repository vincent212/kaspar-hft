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
#include <cctype>
#include <csignal>
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

// 09:30 to 15:00 ET inclusive, every `every_min` minutes.
static std::vector<uint64_t> probe_schedule(const std::string &date_yyyymmdd, int every_min)
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
  // 09:30 to 15:30 ET. These are the window BOUNDARIES, not fire times: the
  // probe quotes continuously between them and each boundary closes one window
  // and opens the next, so N boundaries give N-1 measured windows. The last one
  // closes the final window and stands the lights down.
  for (int mins = 9 * 60 + 30; mins <= 15 * 60 + 30; mins += every_min)
    out.push_back(et_to_epoch_ns(y, mo, d, mins / 60, mins % 60));
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
      ("contract",  po::value<string>()->default_value(""),
                    "single contract to trade, e.g. ESH5; empty = every contract in the universe")
      ("config",    po::value<string>()->default_value("config"),
                    "directory holding som.ini and lights.ini")
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
                    "queue until ts0 + this has passed in MARKET time, so it "
                    "decides how much real flow gets in front of us. Floor is "
                    "40us. -1 = OB's own default (1000us).")
      ("ob-cancel-delay-us", po::value<int>()->default_value(-1),
                    "modelled wire latency for CANCELS, microseconds. A cancel "
                    "goes over the same wire as an order, so -1 (= --ob-delay-us) "
                    "is the right default; set it only to test the asymmetric "
                    "case. Must be >= --ob-delay-us: a cancel is never faster "
                    "than a new order, since the matching engine has to locate "
                    "the resting order before it can pull it.")
      ("probe-size", po::value<int>()->default_value(0),
                    "SlippageProbe size in contracts per leg; 0 = no probe. The "
                    "probe quotes BOTH sides continuously from 09:30 to 15:30 ET "
                    "(BUY lights target +size, SEL lights -size) and measures each "
                    "leg's VWAP against the mid, the touch and the interval VWAP. "
                    "Nothing sells back to flat: inventory is carried and reported "
                    "as pos_at_close.")
      ("probe-out", po::value<string>()->default_value(""),
                    "CSV for the probe's per-fire rows; empty = log only")
      ("probe-date", po::value<string>()->default_value(""),
                    "session date YYYYMMDD for the probe schedule; empty = take it "
                    "from the datafile name. ET wall-clock, DST handled.")
      ("probe-every-min", po::value<int>()->default_value(10),
                    "minutes between window boundaries (09:30-15:30 ET). This is "
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
         << "        --contract ESH5 --config ../config\n";
    return 1;
  }
  if (!vm.count("datafile")) { cerr << "Error: --datafile required\n"; return 1; }
  if (!vm.count("universe")) { cerr << "Error: --universe required\n"; return 1; }

  const auto venue_str = vm["venue"].as<string>();
  const en::x venue = (venue_str == "CMEMDFUT") ? en::x::CMEMDFUT : en::x::CMEMD;

  polonaise::logger::act::Logger::rt = true;
  polonaise::logger::act::Logger::log_debug  = vm.count("logdebug") > 0;
  polonaise::logger::act::Logger::quiet      = vm.count("quiet") > 0;
  polonaise::logger::act::Logger::synchrolog = vm.count("logdebug") > 0;

  const int probe_size = vm["probe-size"].as<int>();
  std::vector<uint64_t> probe_fires;
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
    probe_fires = probe_schedule(pdate, vm["probe-every-min"].as<int>());
    if (probe_fires.empty())
    {
      cerr << "Error: could not build a probe schedule for date " << pdate << "\n";
      return 1;
    }
    // BOUNDARIES, not fires. N boundaries close N-1 windows: the first opens
    // the first window and each later one closes a window and opens the next.
    cerr << "probe schedule: " << probe_fires.size() << " boundaries ("
         << (probe_fires.size() - 1) << " windows) on " << pdate
         << " ET, first=" << probe_fires.front() << " last=" << probe_fires.back() << "\n";
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
                            vm["max-dist"].as<int>(),
                            vm["nlights-per-side"].as<int>(),
                            probe_size,
                            vm["probe-out"].as<string>(),
                            probe_fires);
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
