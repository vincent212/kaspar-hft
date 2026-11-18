/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// bybit_record: record Bybit's public stream for one symbol, raw, with REST
// full_orderbook snapshots, for unit tests and replay. No account needed.
//
//   bybit_record --category spot --symbol SOLBTC --topic full --seconds 20 \
//                --rest-every 5 --out tests/data/spot_SOLBTC_full.rec
//
// --topic full (orderbook.full) or a depth (1, 50, 200, 1000). The output is
// BybitNet's Recorder format: "W <recv_ns> <frame>" and "R <recv_ns> <body>".
// REST snapshots are taken --rest-every seconds (0: none), starting after 1 s.

#include <chrono>
#include <iostream>
#include <string>

#include "bybit_md/BybitNet.hpp"

using namespace bybit;

int main(int argc, char **argv)
{
  std::string category = "linear", symbol = "BTCUSDT", topic = "full", out;
  double seconds = 10;
  int rest_every = 5;
  for (int i = 1; i < argc; ++i)
  {
    std::string k = argv[i];
    auto v = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--category") category = v();
    else if (k == "--symbol") symbol = v();
    else if (k == "--topic") topic = v();
    else if (k == "--seconds") seconds = std::stod(v());
    else if (k == "--rest-every") rest_every = std::stoi(v());
    else if (k == "--out") out = v();
    else
    {
      std::cerr << "unknown argument " << k << std::endl;
      return 2;
    }
  }
  if (out.empty())
  {
    std::cerr << "--out is required" << std::endl;
    return 2;
  }
  bnet::Recorder rec(out);
  if (!rec.ok())
  {
    std::cerr << "cannot write " << out << std::endl;
    return 1;
  }
  bnet::Hosts hosts;
  bnet::PublicWs ws(hosts, category);
  ws.connect({"orderbook." + topic + "." + symbol, "publicTrade." + symbol});
  const auto t0 = std::chrono::steady_clock::now();
  const auto end = t0 + std::chrono::milliseconds(int64_t(seconds * 1000));
  auto next_rest = t0 + std::chrono::seconds(1);
  uint64_t frames = 0, rests = 0;
  while (std::chrono::steady_clock::now() < end)
  {
    rec.ws(sim::wsnet::now_ns(), ws.read());
    ++frames;
    if (rest_every > 0 && std::chrono::steady_clock::now() >= next_rest)
    {
      rec.rest(sim::wsnet::now_ns(), bnet::full_orderbook(hosts, category, symbol));
      ++rests;
      next_rest += std::chrono::seconds(rest_every);
    }
  }
  ws.close();
  std::cerr << "recorded " << frames << " frames, " << rests << " REST snapshots to " << out
            << std::endl;
  return 0;
}
