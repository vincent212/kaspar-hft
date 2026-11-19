/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitFeed's configuration and construction (no network).

#include <gtest/gtest.h>

#include <sstream>

#include <boost/property_tree/ini_parser.hpp>

#include "bybit_md/act/BybitFeed.hpp"

using namespace bybit;

TEST(BybitFeed, ConfigFromIni)
{
  std::istringstream ini(
      "[linear_BTCUSDT]\ntick = 0.1\nchunk = 0.1\nprint_unit = 0.001\nwindow = 500\n"
      "sec_id = 7702\n[spot_BTCUSDT]\ntick = 0.1\nchunk = 0.01\nprint_unit = 0.000001\n"
      "sec_id = 7701\nmode = depth\ndepth = 200\n");
  boost::property_tree::ptree pt;
  boost::property_tree::read_ini(ini, pt);
  auto l = BybitFeed::config_from_ini(pt.get_child("linear_BTCUSDT"), "linear", "BTCUSDT");
  EXPECT_EQ(l.mode, BybitSync::Mode::FULL);
  EXPECT_EQ(l.window, 500u);
  EXPECT_DOUBLE_EQ(l.chunk, 0.1);
  EXPECT_EQ(l.sec_id, 7702);
  auto s = BybitFeed::config_from_ini(pt.get_child("spot_BTCUSDT"), "spot", "BTCUSDT");
  EXPECT_EQ(s.mode, BybitSync::Mode::DEPTH);
  EXPECT_EQ(s.depth, 200);
  EXPECT_EQ(s.window, 1000u);
  EXPECT_EQ(s.hosts.ws_host, "stream.bybit.com");
}

TEST(BybitFeed, ConstructsWithoutAnOB)
{
  BybitFeed::Config c;
  c.category = "spot";
  c.symbol = "BTCUSDT";
  c.tick = 0.1;
  c.chunk = 0.01;
  c.print_unit = 0.000001;
  c.sec_id = 7701;
  BybitFeed f(c, nullptr);
  EXPECT_STREQ(f.get_name(), "BybitFeed_spot_BTCUSDT");
  EXPECT_FALSE(f.sync().synced());
  EXPECT_EQ(f.sync().config().venue, char(en::x::BYBIT));
}
