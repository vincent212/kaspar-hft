/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// kraken_feed -- runs the KrakenFeed actor under a Manager with no OB: the
// monitor mode, printing the top of the book once a second, and optionally
// recording every frame (--record FILE). Stops after --seconds.
//
//   kraken_feed [--symbol BTC/USD] [--depth 1000] [--seconds 60] [--record FILE]

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "actors/act/Manager.hpp"
#include "enum/e_names.hpp"
#include "kraken_md/act/KrakenFeed.hpp"

int main(int argc, char **argv)
{
  kraken::KrakenFeed::Config c;
  c.chunks.tick = 0.1;
  c.chunks.chunk = 0.01;
  c.chunks.sec_id = 9901;
  c.chunks.venue = char(en::x::KRAKEN);
  int seconds = 60;
  for (int i = 1; i + 1 < argc; i += 2)
  {
    std::string k = argv[i], v = argv[i + 1];
    if (k == "--symbol") c.symbol = v;
    else if (k == "--depth") c.depth = c.window = std::stoi(v);
    else if (k == "--seconds") seconds = std::stoi(v);
    else if (k == "--record") c.record = v;
    else
    {
      std::cerr << "usage: kraken_feed [--symbol BTC/USD] [--depth 1000] [--seconds 60] [--record FILE]\n";
      return 2;
    }
  }
  auto feed = new kraken::KrakenFeed(c, nullptr);
  actors::Manager mgr("kraken_feed");
  mgr.manage_and_start(feed);
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  const auto &s = feed->sync().stats();
  std::cout << "snapshots " << s.snapshots << " updates " << s.updates << " checksum ok " << s.checksum_ok
            << " bad " << s.checksum_bad << " trades " << s.trades << " trade_id gaps " << s.trade_id_gaps
            << " reconnects " << feed->reconnects() << std::endl;
  std::_Exit(0); // the feed thread blocks in a read; do not wait for it
}
