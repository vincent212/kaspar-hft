/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * filter_bin -- cut a channel capture down to one asset.
 *
 * A CME channel carries a whole product complex, not one instrument. Channel
 * 318 is "NQ futures" but the capture also holds MNQ and MES, and the result is
 * a bin averaging 1,247 MB against channel 310's 203 MB -- six times the size,
 * nearly all of it order flow the simulator will never trade against. It still
 * pays to replay every message of it.
 *
 * The filter cannot run at extraction time: the converter has only securityIDs,
 * and the map from securityID to symbol IS the universe, which is itself built
 * from the same capture. So this is a second pass -- extract, build the
 * universe, then filter:
 *
 *     filter_bin --in 318.20250102.databento.bin \
 *                --universe universe/318/master_universe.318.json \
 *                --asset NQ --out 318.20250102.NQ.bin
 *
 * WHAT IS KEPT
 *
 *   - every record whose securityID belongs to the asset, and
 *   - every record that has no securityID at all.
 *
 * The second half matters more than it looks. EOB (end of burst), GAP and CHR
 * carry only a venue: they are channel-wide sequencing and recovery markers,
 * not instrument data. EOB in particular is what tells the book a transaction
 * is complete -- the probe's clock runs off it -- so dropping those because
 * they lack a securityID would leave a file that decodes but never settles.
 *
 * Instrument DEFINITIONS (FDF/ODF/SDF/LIM) are kept whatever asset they name.
 * There are very few of them -- they are a header, not a feed -- so filtering
 * them saves nothing measurable, while dropping one risks a downstream lookup
 * failing for an instrument that is mentioned somewhere we did not think of.
 * The bulk being removed is order flow, which is where the six-fold size
 * difference actually lives.
 *
 * WHAT IS NOT DONE. Nothing is rewritten -- no renumbering, no re-timestamping,
 * no reordering. A filtered file is a subsequence of the original, so anything
 * that was true of the order of records is still true.
 */

#include "chutil/Macros.hpp"

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <variant>

#include "bfile/r_l3.hpp"
#include "zlib.h"

namespace po = boost::program_options;
using namespace chutil;

namespace {

// securityIDs whose symbol begins with the asset. Prefix, not equality: the
// symbol is the contract (NQM5), the asset is the product (NQ).
//
// MNQ must NOT match NQ, which a substring test would allow -- hence the
// anchored compare. That is the whole reason this tool takes an asset rather
// than a list of ids.
std::set<int32_t> ids_for_asset(const std::string& universe, const std::string& asset,
                                std::set<std::string>& symbols_out)
{
  std::set<int32_t> out;
  boost::property_tree::ptree pt;
  boost::property_tree::read_json(universe, pt);
  auto insts = pt.get_child_optional("instruments");
  if (!insts) return out;
  for (const auto& kv : *insts) {
    const auto sid = kv.second.get<int32_t>("securityID", 0);
    const auto sym = kv.second.get<std::string>("symbol", "");
    if (!sid || sym.empty()) continue;
    // Prefer the definition's own asset field when it has one; fall back to a
    // prefix of the symbol for universes that predate it.
    const auto a = kv.second.get<std::string>("asset", "");
    const bool match = !a.empty() ? (a == asset)
                                  : (sym.rfind(asset, 0) == 0);
    if (match) { out.insert(sid); symbols_out.insert(sym); }
  }
  return out;
}

// securityID of a record, or 0 when the record is channel-wide (EOB, GAP, CHR
// and the other markers that carry only a venue).
int32_t sec_id_of(const bfile::l3_t& l3)
{
  return std::visit([](const auto& r) -> int32_t {
    if constexpr (requires { r.securityID; }) return static_cast<int32_t>(r.securityID);
    else                                      return 0;
  }, l3);
}

// Is this an instrument DEFINITION? Kept regardless of asset -- see the note at
// the head of the file.
bool is_definition(const bfile::l3_t& l3)
{
  return std::holds_alternative<bfile::l3_fdf_t>(l3)
      || std::holds_alternative<bfile::l3_odf_t>(l3)
      || std::holds_alternative<bfile::l3_sdf_t>(l3)
      || std::holds_alternative<bfile::l3_lim_t>(l3);
}

}  // namespace

int main(int argc, char* argv[])
{
  std::string in, out, universe, asset;
  po::options_description desc("filter_bin options");
  desc.add_options()
    ("help,h", "this message")
    ("in", po::value<std::string>(&in)->required(), "input .bin")
    ("out", po::value<std::string>(&out)->required(), "output .bin")
    ("universe", po::value<std::string>(&universe)->required(), "universe JSON (securityID -> symbol/asset)")
    ("asset", po::value<std::string>(&asset)->required(), "asset to keep, e.g. NQ")
    ("verbose,v", "list the kept symbols");
  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help")) { std::cout << desc << "\n"; return 0; }
    po::notify(vm);
  } catch (const std::exception& e) {
    std::cerr << "filter_bin: " << e.what() << "\n" << desc << "\n";
    return 2;
  }

  std::set<std::string> symbols;
  std::set<int32_t> keep;
  try {
    keep = ids_for_asset(universe, asset, symbols);
  } catch (const std::exception& e) {
    std::cerr << "filter_bin: cannot read universe " << universe << ": " << e.what() << "\n";
    return 2;
  }
  if (keep.empty()) {
    std::cerr << "filter_bin: no instruments matched asset '" << asset << "' in "
              << universe << " -- refusing to write an empty file\n";
    return 3;
  }
  std::cerr << "filter_bin: asset " << asset << " -> " << keep.size() << " securityIDs\n";
  if (vm.count("verbose")) {
    std::cerr << " ";
    for (const auto& s : symbols) std::cerr << " " << s;
    std::cerr << "\n";
  }

  gzFile fin = gzopen(in.c_str(), "rb");
  if (!fin) { std::cerr << "filter_bin: cannot open " << in << "\n"; return 2; }
  gzFile fout = gzopen(out.c_str(), "wb");
  if (!fout) { std::cerr << "filter_bin: cannot create " << out << "\n"; gzclose(fin); return 2; }

  bfile::l3_t rec;
  uint64_t ts = 0;
  uint64_t read = 0, written = 0, global = 0, defs = 0, dropped = 0;
  while (bfile::read_l3(fin, rec, ts)) {
    ++read;
    if (is_definition(rec)) { bfile::write_l3(fout, rec); ++written; ++defs; continue; }
    const int32_t sid = sec_id_of(rec);
    if (sid == 0) { bfile::write_l3(fout, rec); ++written; ++global; continue; }
    if (keep.count(sid)) { bfile::write_l3(fout, rec); ++written; continue; }
    ++dropped;
  }
  gzclose(fin);
  gzclose(fout);

  std::cerr << "filter_bin: read " << read << ", wrote " << written
            << " (" << global << " channel-wide, " << defs << " definitions), dropped "
            << dropped;
  if (read) std::cerr << " (" << (100.0 * written / double(read)) << "% kept)";
  std::cerr << "\n";
  return 0;
}
