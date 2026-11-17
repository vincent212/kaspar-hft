/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// bnsim -- the simulator on live Binance, Coinbase or Hyperliquid data. See
// sim/BINANCE.md and sim/CRYPTO.md.

#include <boost/program_options.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread/thread.hpp>

#include <iostream>
#include <string>

#include "actors/act/Manager.hpp"
#include "enum/e_names.hpp"
#include "logger/act/Logger.hpp"
#include "sim/BnSimKaspr.hpp"
#include "sim/act/BinanceFeed.hpp"

namespace po = boost::program_options;
using std::cerr;
using std::string;

namespace
{
  // --dump: the feed alone, printing the top of the synthetic book each second.
  struct DumpManager : public actors::Manager
  {
    explicit DumpManager(const sim::BnSimKaspr::FeedSpec &spec) : Manager("bnsim_dump")
    {
      add_to_manage_q(spec.make_feed(nullptr));
    }
  };
}

int main(int argc, char **argv)
{
  po::options_description desc("bnsim options");
  desc.add_options()
      ("help", "this message")
      ("exchange", po::value<string>()->default_value("binance"),
       "binance, coinbase or hyperliquid")
      ("config", po::value<string>()->default_value("config_binance"),
       "directory with <exchange>.ini, lights.ini and som.ini")
      ("symbol", po::value<string>()->default_value("BTCUSDT"),
       "a section of <exchange>.ini: BTCUSDT (binance), BTC-USD (coinbase), BTC (hyperliquid)")
      ("futures", "binance: USD-M futures (fstream/fapi) instead of spot")
      ("dump", "feed only: print the top of the synthetic book every second")
      ("probe-size", po::value<int>()->default_value(0),
       "chunks per leg for the SlippageProbe; without it there are no lights, and orders come only over zmq")
      ("probe-window-min", po::value<int>()->default_value(10), "minimum probe window, minutes")
      ("probe-days", po::value<int>()->default_value(7), "how long the probe runs")
      ("probe-out", po::value<string>()->default_value("probe.csv"), "probe CSV")
      ("ob-delay-us", po::value<int>()->default_value(-1), "order latency to the exchange")
      ("ob-feed-delay-us", po::value<int>()->default_value(0), "feed latency from the exchange")
      ("rng-seed", po::value<uint32_t>()->default_value(0), "lights' RNG seed")
      ("mqport", po::value<int>()->default_value(7777),
       "zmq port for orders, BBO and fills (MTD; sim/binance/kaspar_client.py); 0 = none")
      ("quiet", "keep the log small");
  po::variables_map vm;
  try
  {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
  }
  catch (const std::exception &e)
  {
    cerr << e.what() << "\n" << desc << "\n";
    return 1;
  }
  if (vm.count("help"))
  {
    cerr << desc << "\n";
    return 0;
  }

  polonaise::logger::act::Logger::rt = true;
  polonaise::logger::act::Logger::quiet = vm.count("quiet") > 0;

  sim::BnSimKaspr::Options opt;
  opt.exchange = vm["exchange"].as<string>();
  opt.config_dir = vm["config"].as<string>();
  opt.symbol = vm["symbol"].as<string>();
  opt.futures = vm.count("futures") > 0;
  opt.probe_size = vm["probe-size"].as<int>();
  opt.probe_window_min = vm["probe-window-min"].as<int>();
  opt.probe_days = vm["probe-days"].as<int>();
  opt.probe_out = vm["probe-out"].as<string>();
  opt.ob_delay_us = vm["ob-delay-us"].as<int>();
  opt.ob_feed_delay_us = vm["ob-feed-delay-us"].as<int>();
  opt.rng_seed = vm["rng-seed"].as<uint32_t>();
  opt.mqport = vm["mqport"].as<int>();

  actors::Manager *mgr = nullptr;
  try
  {
    if (vm.count("dump"))
      mgr = new DumpManager(sim::BnSimKaspr::FeedSpec::from_config(opt));
    else
      mgr = new sim::BnSimKaspr(opt);
  }
  catch (const std::exception &e)
  {
    cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }

  auto thrd = boost::thread(boost::ref(*mgr));
  thrd.join();
  return 0;
}
