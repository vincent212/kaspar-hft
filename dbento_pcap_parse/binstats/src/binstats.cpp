/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * @file binstats.cpp
 * @brief Statistics analyzer for kaspar .bin market data files.
 *
 * For each gzipped .bin file, reports:
 *   1. FDF / ODF / SDF instrument-definition counts broken down by
 *      updateAction (A=Add, M=Modify, D=Delete, other). A .bin with no
 *      A/M actions is effectively unusable downstream — the symbol table
 *      can't be built, so flagging "FDF count > 0 but no Add/Modify" up
 *      front matters.
 *   2. Per-securityID MBO activity (adds / mods / deletes / trades).
 *   3. A full instrument inventory derived from ANY FDF/ODF/SDF action
 *      (not just A/M) — so even files that only carry definition
 *      Deletes or unexpected action codes still surface the SecurityIDs
 *      and symbols that appear.
 *   4. Counts of other l3 record variants (SST, MBP, MBPT, etc.).
 *
 * Usage: binstats --datafile <path_to_bin_file> [--debug]
 */

#include <iostream>
#include <map>
#include <string>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <variant>
#include <ctime>
#include <boost/program_options.hpp>
#include "bfile/r_l3.hpp"
#include "chutil/Macros.hpp"

namespace po = boost::program_options;

// MBO activity for a SecurityID.
struct SymbolStats {
    std::string name;        // populated from any FDF/ODF/SDF action
    int32_t security_id = 0;
    uint64_t adds = 0;
    uint64_t mods = 0;
    uint64_t deletes = 0;
    uint64_t trades = 0;
};

// Instrument metadata snapshot (last-seen wins).
struct InstrumentInfo {
    int32_t security_id = 0;
    std::string sym;
    std::string asset;
    std::string security_type;
    char def_kind = '?';          // 'F'=FDF, 'O'=ODF, 'S'=SDF
    char last_action = ' ';       // last updateAction seen
    // For options:
    bool is_option = false;
    uint8_t put_or_call = 0;      // 0=Put, 1=Call (CME convention)
    double strike_price = 0.0;
    // For futures and options:
    uint64_t expiration_ns = 0;
};

// Per-record-type action breakdown.
struct ActionCounts {
    uint64_t add = 0;     // 'A'
    uint64_t modify = 0;  // 'M'
    uint64_t del = 0;     // 'D'
    uint64_t other = 0;   // anything else (incl. NUL, space)
    uint64_t total() const { return add + modify + del + other; }
};

static std::string trim(const char* s, size_t maxlen) {
    std::string out(s, strnlen(s, maxlen));
    size_t end = out.find_last_not_of(" \t\n\r\0");
    if (end != std::string::npos) out = out.substr(0, end + 1);
    return out;
}

static void bump_action(ActionCounts& c, char a) {
    switch (a) {
        case 'A': c.add++; break;
        case 'M': c.modify++; break;
        case 'D': c.del++; break;
        default:  c.other++; break;
    }
}

// Names for std::variant<...>::index() values of bfile::l3_t. Must stay in
// sync with the typedef in chutil/include/bfile/r_l3.hpp.
static const char* l3_variant_name(size_t idx) {
    static constexpr const char* names[] = {
        "l3_mbo_v2",                     //  0
        "l3_mbo_trd_v2",                 //  1
        "l3_mbo_snap",                   //  2
        "l3_mbp",                        //  3
        "l3_mbp_trd",                    //  4
        "l3_som",                        //  5
        "l3_volume",                     //  6
        "l3_interval",                   //  7
        "l3_qlen",                       //  8
        "l3_vol",                        //  9  (CME volatility / CVOL update)
        "l3_sst",                        // 10  (security status)
        "l3_fdf",                        // 11
        "l3_odf",                        // 12
        "l3_sdf",                        // 13
        "l3_lim",                        // 14  (price limits)
        "l3_gap_v2",                     // 15
        "l3_chr_v2",                     // 16
        "l3_eob",                        // 17
        "l3_sim",                        // 18
        "l3_fenics_sys_event",           // 19
        "l3_fenics_bdf",                 // 20
        "l3_fenics_trading_action",      // 21
        "l3_fenics_instrumentstats",     // 22
        "l3_dealerweb_bookdir",          // 23
        "l3_dealerweb_sys_event",        // 24
        "l3_dealerweb_orderbookstate",   // 25
        "l3_dealerweb_information",      // 26
        "l3_dealerweb_brokentrade",      // 27
    };
    constexpr size_t N = sizeof(names) / sizeof(names[0]);
    return idx < N ? names[idx] : "(unknown)";
}

static std::string fmt_expiration_ns(uint64_t ns) {
    if (ns == 0) return "(unset)";
    // CME ships nanoseconds since epoch. Truncate to seconds for display.
    std::time_t t = static_cast<std::time_t>(ns / 1'000'000'000ULL);
    std::tm utc{};
    gmtime_r(&t, &utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &utc);
    return std::string(buf);
}

int main(int argc, char** argv) {
    po::options_description desc("binstats - Binary Market Data Statistics");
    desc.add_options()
        ("help,h", "Show help message")
        ("datafile", po::value<std::string>(), "Path to gzipped binary data file")
        ("inventory-only", "Skip the per-symbol MBO table; print only instrument inventory")
        ("max-instruments", po::value<size_t>()->default_value(2000),
                            "Cap the instrument inventory at N rows (sorted by activity)")
        ("debug,d", "Enable debug output");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help") || !vm.count("datafile")) {
        std::cout << desc << std::endl;
        std::cout << "\nExample:\n";
        std::cout << "  binstats --datafile 310.20241230.databento.bin\n";
        return vm.count("help") ? 0 : 1;
    }

    std::string datafile = vm["datafile"].as<std::string>();
    bool debug = vm.count("debug") > 0;
    bool inventory_only = vm.count("inventory-only") > 0;
    size_t max_instruments = vm["max-instruments"].as<size_t>();
    std::cout << "Analyzing binary file: " << datafile << std::endl;

    gzFile file = gzopen(datafile.c_str(), "rb");
    if (!file) {
        std::cerr << "ERROR: Failed to open file: " << datafile << std::endl;
        return 1;
    }

    // Bookkeeping.
    std::map<int32_t, SymbolStats> stats;             // MBO activity per SecurityID
    std::map<int32_t, InstrumentInfo> instruments;    // metadata per SecurityID
    uint64_t total_records = 0;
    uint64_t total_adds = 0, total_mods = 0, total_deletes = 0, total_trades = 0;
    ActionCounts fdf_actions, odf_actions, sdf_actions;
    uint64_t mbo_count = 0, trade_count = 0;
    uint64_t other_record_count = 0;
    std::map<size_t, uint64_t> other_variant_counts;  // variant index -> count

    bfile::l3_t l3;
    uint64_t ts;
    while (bfile::read_l3(file, l3, ts)) {
        total_records++;

        if (std::holds_alternative<bfile::l3_fdf_t>(l3)) {
            const auto& fdf = std::get<bfile::l3_fdf_t>(l3);
            bump_action(fdf_actions, fdf.updateAction);

            // Always record the instrument — A/M/D and "other" actions all
            // contribute to the inventory.
            int32_t sec_id = fdf.securityID;
            auto& info = instruments[sec_id];
            info.security_id = sec_id;
            info.sym = trim(fdf.sym, sizeof(fdf.sym));
            info.asset = trim(fdf.asset, sizeof(fdf.asset));
            info.security_type = trim(fdf.securityType, sizeof(fdf.securityType));
            info.def_kind = 'F';
            info.last_action = fdf.updateAction ? fdf.updateAction : '?';
            info.expiration_ns = fdf.expiration;

            auto& s = stats[sec_id];
            s.security_id = sec_id;
            if (s.name.empty()) s.name = info.sym;

            if (debug && fdf_actions.total() < 20) {
                std::cout << "FDF action=" << (fdf.updateAction ? fdf.updateAction : '?')
                          << " securityID=" << sec_id << " sym=" << info.sym << std::endl;
            }
        }
        else if (std::holds_alternative<bfile::l3_odf_t>(l3)) {
            const auto& odf = std::get<bfile::l3_odf_t>(l3);
            bump_action(odf_actions, odf.updateAction);

            int32_t sec_id = odf.securityID;
            auto& info = instruments[sec_id];
            info.security_id = sec_id;
            info.sym = trim(odf.sym, sizeof(odf.sym));
            info.asset = trim(odf.asset, sizeof(odf.asset));
            info.security_type = trim(odf.securityType, sizeof(odf.securityType));
            info.def_kind = 'O';
            info.last_action = odf.updateAction ? odf.updateAction : '?';
            info.expiration_ns = odf.expiration;
            info.is_option = true;
            info.put_or_call = odf.putOrCall;
            info.strike_price = odf.strikePrice;

            auto& s = stats[sec_id];
            s.security_id = sec_id;
            if (s.name.empty()) s.name = info.sym;
        }
        else if (std::holds_alternative<bfile::l3_sdf_t>(l3)) {
            const auto& sdf = std::get<bfile::l3_sdf_t>(l3);
            bump_action(sdf_actions, sdf.securityUpdateAction);

            int32_t sec_id = sdf.securityID;
            auto& info = instruments[sec_id];
            info.security_id = sec_id;
            info.sym = trim(sdf.sym, sizeof(sdf.sym));
            info.asset = trim(sdf.asset, sizeof(sdf.asset));
            info.security_type = trim(sdf.securityType, sizeof(sdf.securityType));
            info.def_kind = 'S';
            info.last_action = sdf.securityUpdateAction ? sdf.securityUpdateAction : '?';
            info.expiration_ns = sdf.expiration;

            auto& s = stats[sec_id];
            s.security_id = sec_id;
            if (s.name.empty()) s.name = info.sym;
        }
        else if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) {
            mbo_count++;
            const auto& mbo = std::get<bfile::l3_mbo_v2_t>(l3);
            int32_t sec_id = mbo.securityID;

            auto& s = stats[sec_id];
            if (s.name.empty()) {
                // Fall back to a placeholder; will be overwritten if any
                // FDF/ODF/SDF for this sec_id arrives later.
                s.name = "SEC_" + std::to_string(sec_id);
            }
            s.security_id = sec_id;

            switch (mbo.orderUpdateAction) {
                case 0: s.adds++;    total_adds++;    break;  // Add
                case 1: s.mods++;    total_mods++;    break;  // Modify
                case 2: s.deletes++; total_deletes++; break;  // Delete
                default: break;
            }
        }
        else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3)) {
            trade_count++;
            total_trades++;
            // Trade records reference an orderID, not a securityID directly,
            // so we can't attribute them to a SecurityID without an order
            // table. Counted globally.
        }
        else {
            other_record_count++;
            other_variant_counts[l3.index()]++;
        }
    }

    gzclose(file);

    // ===== Header =====
    std::cout << "\n=== Summary for: " << datafile << " ===" << std::endl;
    std::cout << "Total records:        " << total_records << std::endl;

    // ===== Definition action breakdown (THE KEY DIAGNOSTIC) =====
    auto print_actions = [](const std::string& name, const ActionCounts& a) {
        std::cout << "  " << std::left << std::setw(14) << name
                  << "total=" << std::setw(12) << a.total()
                  << " A=" << std::setw(10) << a.add
                  << " M=" << std::setw(10) << a.modify
                  << " D=" << std::setw(10) << a.del
                  << " other=" << a.other << std::endl;
    };
    std::cout << "\n=== Instrument-definition record breakdown ===" << std::endl;
    print_actions("FDF (Futures):", fdf_actions);
    print_actions("ODF (Options):", odf_actions);
    print_actions("SDF (Spreads):", sdf_actions);

    if (fdf_actions.total() + odf_actions.total() + sdf_actions.total() == 0) {
        std::cerr << "\n*** WARNING: ZERO instrument-definition records (FDF/ODF/SDF). "
                     "The .bin is effectively unusable downstream — the symbol "
                     "table cannot be built without at least one Add/Modify per "
                     "active SecurityID. Likely cause: the IR (instrument-replay) "
                     "stream wasn't captured or wasn't being decoded.\n";
    } else if (fdf_actions.add + fdf_actions.modify
             + odf_actions.add + odf_actions.modify
             + sdf_actions.add + sdf_actions.modify == 0) {
        std::cerr << "\n*** WARNING: definition records exist but ZERO have Add/Modify "
                     "actions. Symbol metadata won't be populated; downstream "
                     "consumers will see only SecurityIDs.\n";
    }

    // ===== Other record types =====
    std::cout << "\n=== Other record types ===" << std::endl;
    std::cout << "  MBO (Orders):  " << mbo_count << std::endl;
    std::cout << "  MBOT (Trades): " << trade_count << std::endl;
    if (other_record_count > 0) {
        std::cout << "  Other variants: " << other_record_count << std::endl;
        // Sort by count desc so the dominant non-MBO/non-trade type leads.
        std::vector<std::pair<size_t, uint64_t>> ordered(
            other_variant_counts.begin(), other_variant_counts.end());
        std::sort(ordered.begin(), ordered.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        for (const auto& [idx, n] : ordered) {
            std::cout << "    " << std::left << std::setw(28) << l3_variant_name(idx)
                      << " (variant " << idx << "): " << n << std::endl;
        }
    }

    // ===== Per-symbol MBO activity table (optional) =====
    if (!inventory_only) {
        std::cout << "\n=== Per-SecurityID MBO activity ===" << std::endl;
        std::cout << std::left << std::setw(20) << "Symbol"
                  << std::right << std::setw(12) << "SecurityID"
                  << std::setw(12) << "Adds"
                  << std::setw(12) << "Mods"
                  << std::setw(12) << "Deletes"
                  << std::setw(12) << "Trades" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        std::vector<SymbolStats> sorted_stats;
        for (const auto& [sec_id, stat] : stats) {
            if (stat.adds > 0 || stat.mods > 0 || stat.deletes > 0 || stat.trades > 0) {
                sorted_stats.push_back(stat);
            }
        }
        std::sort(sorted_stats.begin(), sorted_stats.end(),
                  [](const SymbolStats& a, const SymbolStats& b) {
                      return a.adds < b.adds;
                  });
        for (const auto& s : sorted_stats) {
            std::cout << std::left << std::setw(20) << s.name
                      << std::right << std::setw(12) << s.security_id
                      << std::setw(12) << s.adds
                      << std::setw(12) << s.mods
                      << std::setw(12) << s.deletes
                      << std::setw(12) << s.trades << std::endl;
        }
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::left << std::setw(20) << "TOTAL"
                  << std::right << std::setw(12) << ""
                  << std::setw(12) << total_adds
                  << std::setw(12) << total_mods
                  << std::setw(12) << total_deletes
                  << std::setw(12) << total_trades << std::endl;
    }

    // ===== Instrument inventory =====
    std::cout << "\n=== Instrument inventory (" << instruments.size() << " SecurityIDs) ===" << std::endl;
    std::cout << std::left
              << std::setw(12) << "SecID"
              << std::setw(2)  << "K"     // F/O/S
              << std::setw(2)  << "A"     // last action
              << std::setw(20) << "Symbol"
              << std::setw(10) << "Asset"
              << std::setw(10) << "Type"
              << std::setw(12) << "Expires"
              << std::setw(8)  << "P/C"
              << std::setw(12) << "Strike" << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    // Sort by MBO activity descending so the most-traded instruments lead.
    std::vector<InstrumentInfo> sorted_inv;
    sorted_inv.reserve(instruments.size());
    for (const auto& [_, v] : instruments) sorted_inv.push_back(v);
    std::sort(sorted_inv.begin(), sorted_inv.end(),
              [&](const InstrumentInfo& a, const InstrumentInfo& b) {
                  uint64_t aw = stats.count(a.security_id) ? stats[a.security_id].adds : 0;
                  uint64_t bw = stats.count(b.security_id) ? stats[b.security_id].adds : 0;
                  if (aw != bw) return aw > bw;
                  return a.security_id < b.security_id;
              });

    size_t shown = 0;
    for (const auto& i : sorted_inv) {
        if (shown >= max_instruments) {
            std::cout << "  ... (" << (sorted_inv.size() - shown)
                      << " more instruments truncated; pass --max-instruments=N "
                         "to see more)" << std::endl;
            break;
        }
        std::cout << std::left
                  << std::setw(12) << i.security_id
                  << std::setw(2)  << i.def_kind
                  << std::setw(2)  << (i.last_action ? i.last_action : '?')
                  << std::setw(20) << (i.sym.empty() ? "(no-sym)" : i.sym)
                  << std::setw(10) << i.asset
                  << std::setw(10) << i.security_type
                  << std::setw(12) << fmt_expiration_ns(i.expiration_ns);
        if (i.is_option) {
            std::cout << std::setw(8)  << (i.put_or_call == 0 ? "Put" : (i.put_or_call == 1 ? "Call" : "?"))
                      << std::setw(12) << i.strike_price;
        }
        std::cout << std::endl;
        ++shown;
    }

    // Sanity-check: SecurityIDs touched by MBO but without any def record.
    size_t orphan_count = 0;
    for (const auto& [sec_id, _] : stats) {
        if (!instruments.count(sec_id)) ++orphan_count;
    }
    if (orphan_count > 0) {
        std::cerr << "\n*** WARNING: " << orphan_count << " SecurityID(s) have MBO "
                     "activity but no FDF/ODF/SDF definition record in this .bin. "
                     "Downstream consumers cannot resolve symbols / strikes / "
                     "expirations for these instruments. Likely cause: IR stream "
                     "wasn't captured for the full window covered by the file.\n";
    }

    return 0;
}
