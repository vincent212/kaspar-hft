/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include "chutil/sig_hand.hpp"
#include "sim/SimKaspr.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>

#include <csignal>
#include <fenv.h>
#include <iostream>
#include <unistd.h>

namespace po = boost::program_options;
using namespace std;

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
                    "case. Must be >= --ob-delay-us: the delay queue is FIFO, so "
                    "a cancel cannot overtake an order still in flight and a "
                    "shorter value would silently do nothing.")
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
  polonaise::logger::act::Logger::synchrolog = vm.count("logdebug") > 0;

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
                            vm["ob-cancel-delay-us"].as<int>());
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
