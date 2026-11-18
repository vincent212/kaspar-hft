#pragma once
// Configuration of one market's feed: an ini section (config/nasdaq_md.ini).
#include "nasdaq_md/Dispatcher.hpp"
#include "nasdaq_md/MoldSequencer.hpp"
#include "nasdaq_md/Time.hpp"

#include <boost/property_tree/ptree.hpp>

#include <set>
#include <string>

namespace nasdaq_md
{
struct FeedConfig
{
  std::string name = "NASDAQ";          // actor name suffix
  Dispatcher::Config dispatch;          // venue, market code, trading date
  MoldSequencer::Config sequencer;
  // Instruments
  std::set<std::string> symbols;        // empty = every live symbol
  bool include_test_issues = false;
  std::string asset_prefix;             // Kaspar asset name = prefix + symbol
  int32_t sec_id_base = 1900000000;
  // Multicast (A and B), via mcast_recv::SocketReader<uint64_t,10>
  std::string group_a, group_b, interface;
  uint16_t port_a = 0, port_b = 0;
  // MoldUDP64 re-request server
  std::string rerequest_host;
  uint16_t rerequest_port = 0;
  // GLIMPSE
  std::string glimpse_host, glimpse_user, glimpse_password;
  uint16_t glimpse_port = 0;
  bool fast_send = true;                // fast_send to the books (handler_if's UseFastSend)
};

// market = NASDAQ | NASDAQBX | NASDAQPSX; date = YYYYMMDD (trading date).
// Throws std::runtime_error on a missing or malformed key.
FeedConfig config_from_ini(const boost::property_tree::ptree &section);
} // namespace nasdaq_md
