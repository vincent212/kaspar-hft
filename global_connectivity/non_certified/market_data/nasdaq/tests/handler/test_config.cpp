// config/nasdaq_md.ini parses into a FeedConfig; the trading date gives
// midnight ET.
#include "nasdaq_md/Config.hpp"

#include <boost/property_tree/info_parser.hpp>
#include <gtest/gtest.h>

#include <cstdlib>

TEST(Config, TheExampleIniParses)
{
  const char *root = std::getenv("KSPRPROJ");
  ASSERT_NE(root, nullptr);
  boost::property_tree::ptree pt;
  boost::property_tree::read_info(
      std::string(root) + "/global_connectivity/non_certified/market_data/nasdaq/config/nasdaq_md.ini", pt);
  const auto c = nasdaq_md::config_from_ini(pt.get_child("NASDAQ"));
  EXPECT_EQ(c.dispatch.venue, char(en::x::NASDAQ));
  EXPECT_EQ(c.dispatch.market_code, 'Q');
  EXPECT_EQ(c.dispatch.midnight_ns, 1781236800000000000ull);
  EXPECT_EQ(c.symbols, (std::set<std::string>{"AAPL", "MSFT"}));
  EXPECT_EQ(c.sequencer.retry_ns, 50000000u);
  EXPECT_TRUE(c.sequencer.snapshot_on_late_join);
  EXPECT_TRUE(c.glimpse_host.empty());
}

TEST(Config, BadMarketAndDateAreRefused)
{
  boost::property_tree::ptree s;
  s.put("market", "NYSE");
  s.put("date", "20260612");
  EXPECT_THROW(nasdaq_md::config_from_ini(s), std::runtime_error);
  s.put("market", "NASDAQBX");
  s.put("date", "2026-06-12");
  EXPECT_THROW(nasdaq_md::config_from_ini(s), std::runtime_error);
  s.put("date", "20260115");
  const auto c = nasdaq_md::config_from_ini(s);
  EXPECT_EQ(c.dispatch.market_code, 'B');
  EXPECT_EQ(c.dispatch.midnight_ns, 1768453200000000000ull);   // EST
}
