/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * volstats — per-symbol volume for one session, so the front contract can be
 * chosen from the data instead of a hardcoded roll calendar.
 *
 * Emits volume.<chan>.<date>.json with one entry per securityID, sorted by
 * volume descending, so entry [0] is the front month for that session.
 *
 * Volume source: l3_vol records, which carry (securityID, vol) — CME's
 * per-instrument volume statistic. The trade records (l3_mbo_trd_v2_t) cannot
 * be used directly: they carry orderID but NOT securityID, so attributing a
 * trade to an instrument would require replaying the whole order map.
 *
 * Both the running max and the last sighting are reported: if the statistic is
 * cumulative they agree, and if it is ever reset intraday the difference says
 * so rather than silently under-reporting.
 */

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "chutil/Macros.hpp"
#include "bfile/r_l3.hpp"

namespace po = boost::program_options;
namespace fs = boost::filesystem;

namespace {

struct Stat {
  int64_t  vol_max  = 0;   // highest sighting (session total if cumulative)
  int64_t  vol_last = 0;   // last sighting
  uint64_t records  = 0;   // how many l3_vol records
  uint64_t adds     = 0;   // MBO adds, as an activity cross-check
  uint64_t trades   = 0;   // MBO trade-action count
  std::string symbol;
};

// securityID -> symbol, from a universe JSON (optional but makes the output
// readable and is what downstream actually keys on).
std::map<int32_t, std::string> load_symbols(const std::string& path)
{
  std::map<int32_t, std::string> out;
  if (path.empty()) return out;
  boost::property_tree::ptree pt;
  try { boost::property_tree::read_json(path, pt); }
  catch (const std::exception& e) {
    std::cerr << "volstats: cannot read universe " << path << ": " << e.what() << "\n";
    return out;
  }
  if (auto insts = pt.get_child_optional("instruments"))
    for (const auto& kv : *insts) {
      const auto sid = kv.second.get<int32_t>("securityID", 0);
      const auto sym = kv.second.get<std::string>("symbol", "");
      if (sid && !sym.empty()) out[sid] = sym;
    }
  return out;
}

} // namespace

int main(int argc, char* argv[])
{
  SET_ARGS;

  po::options_description desc("volstats options");
  desc.add_options()
      ("help",     "produce help message")
      ("datafile", po::value<std::string>(), "gzip'd L3 .bin to scan (required)")
      ("universe", po::value<std::string>()->default_value(""),
                   "universe JSON, to name the securityIDs")
      ("out",      po::value<std::string>()->default_value(""),
                   "output path (default: volume.<chan>.<date>.json beside the cwd)")
      ("top",      po::value<int>()->default_value(10), "how many to print to stderr");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help") || !vm.count("datafile")) {
    std::cout << desc << "\n"
              << "Example:\n"
              << "  volstats --datafile 310.20250115.databento.bin \\\n"
              << "           --universe universe.310.20250115.json\n";
    return 1;
  }

  const auto file = vm["datafile"].as<std::string>();
  auto symbols = load_symbols(vm["universe"].as<std::string>());

  // chan/date from the filename: <chan>.<date>.databento.bin
  std::string chan = "unknown", date = "unknown";
  {
    std::vector<std::string> parts;
    boost::split(parts, fs::path(file).filename().string(), boost::is_any_of("."));
    if (parts.size() >= 2) { chan = parts[0]; date = parts[1]; }
  }

  gzFile f = gzopen(file.c_str(), "rb");
  if (!f) ERRF(boost::format("cannot open %s") % file);

  std::map<int32_t, Stat> stats;
  std::map<uint64_t, int32_t> order_sec;   // orderID -> securityID, to attribute trades
  bfile::l3_t l3;
  uint64_t ts = 0;
  long n = 0;

  while (bfile::read_l3(f, l3, ts)) {
    ++n;
    if (std::holds_alternative<bfile::l3_vol_t>(l3)) {
      const auto& v = std::get<bfile::l3_vol_t>(l3);
      auto& s = stats[v.securityID];
      s.vol_max = std::max<int64_t>(s.vol_max, v.vol);
      s.vol_last = v.vol;
      ++s.records;
    } else if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) {
      const auto& m = std::get<bfile::l3_mbo_v2_t>(l3);
      auto& s = stats[m.securityID];
      if (m.orderUpdateAction == 0) { ++s.adds; order_sec[m.orderID] = m.securityID; }
      else if (m.orderUpdateAction == 2) order_sec.erase(m.orderID);
    } else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3)) {
      // No securityID on the trade record — attribute via the order map.
      const auto& t = std::get<bfile::l3_mbo_trd_v2_t>(l3);
      auto it = order_sec.find(t.orderID);
      if (it != order_sec.end()) ++stats[it->second].trades;
    }
  }
  gzclose(f);

  for (auto& kv : stats) {
    auto it = symbols.find(kv.first);
    kv.second.symbol = (it == symbols.end()) ? "" : it->second;
  }

  // Rank by volume; [0] is the front month.
  std::vector<std::pair<int32_t, Stat>> ranked(stats.begin(), stats.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second.vol_max > b.second.vol_max; });

  std::string out = vm["out"].as<std::string>();
  if (out.empty()) out = "volume." + chan + "." + date + ".json";
  std::ofstream o(out);
  ASSERTF(o.is_open(), boost::format("cannot write %s") % out);

  o << "{\n \"channel\": \"" << chan << "\",\n \"date\": \"" << date << "\",\n";
  if (!ranked.empty() && !ranked[0].second.symbol.empty())
    o << " \"front\": \"" << ranked[0].second.symbol << "\",\n";
  if (!ranked.empty())
    o << " \"front_security_id\": " << ranked[0].first << ",\n";
  o << " \"instruments\": [\n";
  for (size_t i = 0; i < ranked.size(); ++i) {
    const auto& [sid, s] = ranked[i];
    o << "  {\"securityID\": " << sid
      << ", \"symbol\": \"" << s.symbol << "\""
      << ", \"volume\": " << s.vol_max
      << ", \"volume_last\": " << s.vol_last
      << ", \"vol_records\": " << s.records
      << ", \"mbo_adds\": " << s.adds
      << ", \"trades\": " << s.trades << "}"
      << (i + 1 < ranked.size() ? ",\n" : "\n");
  }
  o << " ]\n}\n";
  o.close();

  std::cerr << "volstats: " << n << " records, " << ranked.size()
            << " instruments -> " << out << "\n";
  const int top = vm["top"].as<int>();
  for (int i = 0; i < top && i < int(ranked.size()); ++i) {
    const auto& [sid, s] = ranked[i];
    std::cerr << "  " << (s.symbol.empty() ? "?" : s.symbol)
              << " secID=" << sid << " vol=" << s.vol_max
              << " adds=" << s.adds << " trades=" << s.trades << "\n";
  }
  return 0;
}
