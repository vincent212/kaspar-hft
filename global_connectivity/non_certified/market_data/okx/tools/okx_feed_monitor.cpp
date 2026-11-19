/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// okx_feed_monitor: runs the OkxFeed actor itself (its connection loop,
// reconnects and OkxSync) under a kaspar Manager with no OB, printing each
// instrument's top of book once a second. No account needed.
//
//   okx_feed_monitor [--config config/okx_example.ini] [--seconds N]

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "actors/act/Manager.hpp"
#include "enum/e_names.hpp"
#include "okx_md/act/OkxFeed.hpp"

int main(int argc, char **argv)
{
  std::string config = "config/okx_example.ini";
  int seconds = 60;
  for (int i = 1; i + 1 < argc; i += 2)
  {
    std::string a = argv[i];
    if (a == "--config")
      config = argv[i + 1];
    else if (a == "--seconds")
      seconds = std::stoi(argv[i + 1]);
  }
  boost::property_tree::ptree pt;
  boost::property_tree::read_ini(config, pt);
  actors::Manager mgr("okx_feed_monitor");
  std::vector<okx::md::OkxFeed *> feeds;
  for (const auto &[name, s] : pt)
  {
    if (name.rfind("instrument.", 0) != 0)
      continue;
    const char venue = char(s.get<std::string>("inst_type") == "SPOT" ? en::x::OKX : en::x::OKXF);
    auto c = okx::md::OkxFeed::config_from_ini(s, venue);
    feeds.push_back(new okx::md::OkxFeed(c, nullptr));
    mgr.manage_and_start(feeds.back());
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 3 connections per second per IP
  }
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  for (auto *f : feeds)
  {
    const auto &st = f->sync().stats();
    std::cout << "FINAL " << f->sync().inst_id() << " snapshots " << st.snapshots << " updates " << st.updates
              << " gaps " << st.gaps << " crossed " << st.crossed << " trades " << st.trades << " checksum_zero "
              << st.checksum_zero << " checksum_mismatch " << st.checksum_mismatch << std::endl;
  }
  std::_Exit(0); // the feeds' read loops block in the socket; nothing to flush
}
