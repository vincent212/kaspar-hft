/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * build_universe.cpp - Extract instrument definitions from CME .bin files
 *
 * Scans .bin file for FDF (futures), ODF (options), and SDF (spreads) messages,
 * plus the price-limit records (LIM: high/low limit and max price variation),
 * and creates the files below. When CME sends no high limit (Treasury futures
 * have none), high_limit_px defaults to twice the median traded price in the
 * file; high_limit_source says which it is ("cme" or "default").
 * Creates:
 *   - universe.{chan}.{date}.{timestamp}.csv - parsed fields
 *   - universe.{chan}.{date}.{timestamp}.json - complete FDF/ODF/SDF messages
 *
 * Usage:
 *   ./build_universe /path/to/data.310.20260319.1234567890.bin.gz
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <zlib.h>
#include "bfile/r_l3.hpp"
#include "nlohmann/json.hpp"
#include "enum/e_names.hpp"

namespace fs = boost::filesystem;
using json = nlohmann::json;

struct UniverseEntry {
    std::string type;  // "FDF", "ODF", or "SDF"
    int32_t securityID;
    std::string symbol;
    char venue;
    std::string asset;
    double minPriceIncrement;
    double minCabPrice;
    double dispFactor;
    std::string securityType;
    double def_high_limit_px = 0.0;  // limits carried on the definition itself
    double def_low_limit_px = 0.0;

    // Full message for JSON output
    json full_message;
};

class UniverseBuilder {
private:
    std::string bin_file_;
    std::string chan_;
    std::string date_;
    std::string timestamp_;
    std::map<int32_t, UniverseEntry> entries_;  // key = securityID

    // Sanitize string for JSON (remove non-printable characters)
    std::string sanitize(const char* str, size_t max_len) {
        std::string result;
        for (size_t i = 0; i < max_len && str[i] != '\0'; ++i) {
            if (std::isprint(static_cast<unsigned char>(str[i]))) {
                result += str[i];
            }
        }
        return result;
    }

    // Check if a double value is a sentinel (uninitialized/max value)
    // Sentinel is typically INT64_MAX / 1e9 ≈ 9.223e9
    json sanitize_price(double value) {
        if (value > 1e8) {
            return nullptr;  // Output as null in JSON
        }
        return value;
    }

    // Price limits per securityID, from l3_lim records (MDP3
    // MDIncrementalRefreshLimitsBanding). Each field is kept independently, as
    // the last USABLE value seen: a record often carries only some of the three
    // (e.g. a band with null high/low), and keeping the whole last record let a
    // later record with a null high wipe out an earlier valid one.
    struct Limits {
        double pxh = 0.0;     // high limit, 0 = never seen
        double pxl = 0.0;     // low limit
        double pxvar = 0.0;   // max price variation (the band)
        uint32_t records = 0; // l3_lim records seen for this securityID
    };
    std::map<int32_t, Limits> limits_;

    static bool usable_px(double v) { return v > 0.0 && v < 1e8; }

    void add_lim(const bfile::l3_lim_t& lim) {
        auto& l = limits_[lim.securityID];
        ++l.records;
        if (usable_px(lim.pxh)) l.pxh = lim.pxh;
        if (usable_px(lim.pxl)) l.pxl = lim.pxl;
        if (usable_px(lim.pxvar)) l.pxvar = lim.pxvar;
    }

    // Limit price for a securityID, preferring the l3_lim records and falling
    // back to whatever the definition carried. Returns null when neither is
    // usable, so the consumer can apply its own default.
    json limit_px(int32_t sec_id, bool high, double def_from_definition) {
        auto it = limits_.find(sec_id);
        if (it != limits_.end()) {
            const double v = high ? it->second.pxh : it->second.pxl;
            if (usable_px(v)) return v;
        }
        return sanitize_price(def_from_definition);
    }

    // Traded prices per securityID, as a histogram (price -> number of trade
    // records), in the file's native price units (the same units as
    // minPriceIncrement and the limits). A session trades at a few hundred
    // distinct prices at most, so this stays small.
    std::map<int32_t, std::map<double, uint64_t>> traded_px_;

    void see_px(int32_t sec_id, double px) {
        if (!usable_px(px)) return;
        ++traded_px_[sec_id][px];
    }

    static double median_px(const std::map<double, uint64_t>& h) {
        uint64_t n = 0;
        for (const auto& kv : h) n += kv.second;
        uint64_t seen = 0;
        for (const auto& kv : h) {
            seen += kv.second;
            if (2 * seen >= n) return kv.first;
        }
        return 0.0;
    }

    // orderID -> (securityID, price) for every order still in the book.
    std::unordered_map<uint64_t, std::pair<int32_t, double>> live_orders_;

    // Default high limit when CME sends none (the SBE null): twice the MEDIAN
    // traded price in this file. CME publishes no daily high limit for Treasury
    // futures, only a price band, and OB cannot size its price ladder without a
    // top. The median, not the maximum: a few trade records point at an order
    // priced far from the market (ZNZ6 on 2026-10-18 showed a "trade" at
    // 105275), and one of those would size the ladder at millions of ticks.
    // Twice the median covers any real session move. No trades means no
    // default: the instrument stays null. (Orders priced above the ladder are
    // dropped by OB, which logs each one.)
    static constexpr double kDefaultHighMultiple = 2.0;

    json max_price_variation(int32_t sec_id) {
        auto it = limits_.find(sec_id);
        if (it != limits_.end() && usable_px(it->second.pxvar)) return it->second.pxvar;
        return nullptr;
    }

    uint32_t limit_records(int32_t sec_id) {
        auto it = limits_.find(sec_id);
        return it == limits_.end() ? 0 : it->second.records;
    }

    // Limits arrive throughout the file, often after the definition. Fill them
    // in once the whole file has been read, so every instrument sees the
    // final values whatever the record order.
    void apply_limits() {
        for (auto& [secID, entry] : entries_) {
            auto& m = entry.full_message;
            json high = limit_px(secID, true, entry.def_high_limit_px);
            const auto tp = traded_px_.find(secID);
            const bool traded = tp != traded_px_.end() && !tp->second.empty();
            if (!high.is_null())
                m["high_limit_source"] = "cme";
            else if (traded) {
                high = kDefaultHighMultiple * median_px(tp->second);
                m["high_limit_source"] = "default";
            } else
                m["high_limit_source"] = nullptr;
            m["high_limit_px"] = high;
            m["median_traded_px"] = traded ? json(median_px(tp->second)) : json(nullptr);
            m["max_traded_px"] = traded ? json(tp->second.rbegin()->first) : json(nullptr);
            m["low_limit_px"] = limit_px(secID, false, entry.def_low_limit_px);
            m["max_price_variation"] = max_price_variation(secID);
            m["limit_records"] = limit_records(secID);
        }
    }

    void parse_filename() {
        fs::path p(bin_file_);
        std::string filename = p.filename().string();

        std::vector<std::string> parts;
        boost::split(parts, filename, boost::is_any_of("."));

        // Format: {chan}.{date}.{timestamp}.{...}.bin or .bin.gz
        if (parts.size() >= 3) {
            chan_ = parts[0];
            date_ = parts[1];
            timestamp_ = parts[2];
        } else {
            std::cerr << "WARNING: Could not parse filename, using defaults" << std::endl;
            chan_ = "unknown";
            date_ = "unknown";
            timestamp_ = "0";
        }

        std::cerr << "Parsed: chan=" << chan_
                  << " date=" << date_
                  << " timestamp=" << timestamp_ << std::endl;
    }

    void add_fdf(const bfile::l3_fdf_t& fdf) {
        UniverseEntry entry;
        entry.type = "FDF";
        entry.securityID = fdf.securityID;
        entry.symbol = sanitize(fdf.sym, 24);
        entry.venue = fdf.venue;
        entry.asset = sanitize(fdf.asset, 8);
        entry.minPriceIncrement = fdf.minPriceIncrement;
        entry.minCabPrice = 0.0;  // Not used for futures
        entry.dispFactor = fdf.dispFactor;
        entry.securityType = sanitize(fdf.securityType, 16);
        entry.def_high_limit_px = fdf.high_limit_px;
        entry.def_low_limit_px = fdf.low_limit_px;

        // Build JSON for full message
        entry.full_message = {
            {"type", "FDF"},
            {"securityID", fdf.securityID},
            {"symbol", entry.symbol},
            {"asset", entry.asset},
            {"venue", en::to_string(en::x(fdf.venue))},
            {"cfiCode", sanitize(fdf.cfiCode, 8)},
            {"minPriceIncrement", sanitize_price(fdf.minPriceIncrement)},
            {"dispFactor", fdf.dispFactor},
            {"securityType", entry.securityType},
            {"maturityMonth", fdf.maturityMont},
            {"maturityYear", fdf.maturityYear},
            {"unitOfMeasure", sanitize(fdf.unitOfMeasure, 16)}
        };

        entries_[fdf.securityID] = entry;
    }

    void add_odf(const bfile::l3_odf_t& odf) {
        UniverseEntry entry;
        entry.type = "ODF";
        entry.securityID = odf.securityID;
        entry.symbol = sanitize(odf.sym, 24);
        entry.venue = odf.venue;
        entry.asset = sanitize(odf.asset, 8);
        entry.minPriceIncrement = odf.minPriceIncrement;
        entry.minCabPrice = odf.minCabPrice;
        entry.dispFactor = odf.dispFactor;
        entry.securityType = sanitize(odf.securityType, 16);
        entry.def_high_limit_px = odf.high_limit_px;
        entry.def_low_limit_px = odf.low_limit_px;

        // Build JSON for full message
        entry.full_message = {
            {"type", "ODF"},
            {"securityID", odf.securityID},
            {"symbol", entry.symbol},
            {"asset", entry.asset},
            {"venue", en::to_string(en::x(odf.venue))},
            {"cfiCode", sanitize(odf.cfiCode, 8)},
            {"minPriceIncrement", sanitize_price(odf.minPriceIncrement)},
            {"minCabPrice", sanitize_price(odf.minCabPrice)},
            {"dispFactor", odf.dispFactor},
            {"securityType", entry.securityType},
            {"putOrCall", odf.putOrCall},
            {"strikePrice", odf.strikePrice},
            {"underlyingSecurityID", std::vector<uint32_t>(odf.underlyingSecurityID, odf.underlyingSecurityID + 4)},
            {"maturityMonth", odf.maturityMont},
            {"maturityYear", odf.maturityYear}
        };

        entries_[odf.securityID] = entry;
    }

    void add_sdf(const bfile::l3_sdf_t& sdf) {
        UniverseEntry entry;
        entry.type = "SDF";
        entry.securityID = sdf.securityID;
        entry.symbol = sanitize(sdf.sym, 24);
        entry.venue = sdf.venue;
        entry.asset = sanitize(sdf.asset, 8);
        entry.minPriceIncrement = sdf.minPriceIncrement;
        entry.minCabPrice = 0.0;  // Not used for spreads
        entry.dispFactor = sdf.dispFactor;
        entry.securityType = sanitize(sdf.securityType, 16);
        entry.def_high_limit_px = sdf.high_limit_px;
        entry.def_low_limit_px = sdf.low_limit_px;

        // Build JSON for full message
        entry.full_message = {
            {"type", "SDF"},
            {"securityID", sdf.securityID},
            {"symbol", entry.symbol},
            {"asset", entry.asset},
            {"venue", en::to_string(en::x(sdf.venue))},
            {"cfiCode", sanitize(sdf.cfiCode, 8)},
            {"minPriceIncrement", sanitize_price(sdf.minPriceIncrement)},
            {"dispFactor", sdf.dispFactor},
            {"securityType", entry.securityType},
            {"noLegsCount", sdf.noLegsCount},
            {"legSecurityID", std::vector<int32_t>(sdf.legSecurityID, sdf.legSecurityID + 8)},
            {"legPrice", std::vector<double>(sdf.legPrice, sdf.legPrice + 8)},
            {"legSide", std::vector<uint8_t>(sdf.legSide, sdf.legSide + 8)},
            {"maturityMonth", sdf.maturityMont},
            {"maturityYear", sdf.maturityYear}
        };

        entries_[sdf.securityID] = entry;
    }

public:
    UniverseBuilder(const std::string& bin_file) : bin_file_(bin_file) {
        parse_filename();
    }

    void scan() {
        std::cerr << "Scanning " << bin_file_ << " for instrument definitions..." << std::endl;

        gzFile file = gzopen(bin_file_.c_str(), "rb");
        if (!file) {
            throw std::runtime_error("Failed to open file: " + bin_file_);
        }

        size_t msg_count = 0;
        size_t fdf_count = 0, odf_count = 0, sdf_count = 0, lim_count = 0;

        bfile::l3_t l3;
        uint64_t ts;

        while (bfile::read_l3(file, l3, ts)) {
            msg_count++;

            if (std::holds_alternative<bfile::l3_fdf_t>(l3)) {
                add_fdf(std::get<bfile::l3_fdf_t>(l3));
                fdf_count++;
            } else if (std::holds_alternative<bfile::l3_odf_t>(l3)) {
                add_odf(std::get<bfile::l3_odf_t>(l3));
                odf_count++;
            } else if (std::holds_alternative<bfile::l3_lim_t>(l3)) {
                // Price limits arrive as their own record, separate from the
                // instrument definition; OB sizes its price ladder from them.
                add_lim(std::get<bfile::l3_lim_t>(l3));
                lim_count++;
            } else if (std::holds_alternative<bfile::l3_sdf_t>(l3)) {
                add_sdf(std::get<bfile::l3_sdf_t>(l3));
                sdf_count++;
            }

            // Traded prices, for the default high limit (see
            // kDefaultHighMultiple). A trade record (l3_mbo_trd_v2_t) carries
            // the resting order's id but no price and no securityID, so both
            // come from the book: every live order's price is tracked from
            // its add/change and dropped on delete. Resting prices themselves
            // are NOT used: with no daily limit CME accepts resting orders far
            // from the market (ZNZ6 had a sell resting at 10426), and one of
            // those would size the ladder at millions of ticks.
            std::visit([this](const auto& r) {
                using R = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<R, bfile::l3_mbo_v2_t> ||
                              std::is_same_v<R, bfile::l3_mbo_snap_t>) {
                    if (r.orderUpdateAction == 2)   // MDP3 OrderUpdateAction: 0 new, 1 change, 2 delete
                        live_orders_.erase(r.orderID);
                    else
                        live_orders_[r.orderID] = {r.securityID, r.pxd};
                } else if constexpr (std::is_same_v<R, bfile::l3_mbo_trd_v2_t>) {
                    auto it = live_orders_.find(r.orderID);
                    if (it != live_orders_.end())
                        see_px(it->second.first, it->second.second);
                } else if constexpr (std::is_same_v<R, bfile::l3_mbp_trd_t>) {
                    see_px(r.securityID, r.pxd);
                }
            }, l3);

            // Progress update every 1M messages
            if (msg_count % 1000000 == 0) {
                std::cerr << "Processed " << msg_count << " messages..." << std::endl;
            }
        }

        gzclose(file);
        apply_limits();

        std::cerr << "Scan complete: " << msg_count << " messages" << std::endl;
        std::cerr << "  FDF: " << fdf_count << std::endl;
        std::cerr << "  ODF: " << odf_count << std::endl;
        std::cerr << "  SDF: " << sdf_count << std::endl;
        std::cerr << "  LIM: " << lim_count << " records for " << limits_.size() << " securityIDs" << std::endl;
        std::cerr << "  Total instruments: " << entries_.size() << std::endl;
    }

    void write_csv() {
        std::string filename = "universe." + chan_ + "." + date_ + "." + timestamp_ + ".csv.gz";
        gzFile out = gzopen(filename.c_str(), "wb");
        if (!out) {
            throw std::runtime_error("Failed to open CSV file for writing: " + filename);
        }

        // CSV header
        const char* header = "type,securityID,symbol,venue,asset,minPriceIncrement,minCabPrice,dispFactor,tickSize,securityType\n";
        gzwrite(out, header, strlen(header));

        // Write entries sorted by securityID
        for (const auto& [secID, entry] : entries_) {
            char line[512];

            // Calculate effective tick size based on type
            double tick_size = 0.0;
            if (entry.type == "ODF") {
                // Options use minCabPrice * dispFactor
                tick_size = entry.minCabPrice * entry.dispFactor;
            } else if (entry.minPriceIncrement < 1e8 && entry.minPriceIncrement > 0) {
                // Futures/Spreads use minPriceIncrement * dispFactor
                tick_size = entry.minPriceIncrement * entry.dispFactor;
            }

            int len = snprintf(line, sizeof(line), "%s,%d,%s,%c,%s,%.15g,%.15g,%.15g,%.15g,%s\n",
                              entry.type.c_str(),
                              entry.securityID,
                              entry.symbol.c_str(),
                              entry.venue,
                              entry.asset.c_str(),
                              entry.minPriceIncrement,
                              entry.minCabPrice,
                              entry.dispFactor,
                              tick_size,
                              entry.securityType.c_str());
            gzwrite(out, line, len);
        }

        gzclose(out);
        std::cerr << "Wrote " << filename << " (" << entries_.size() << " instruments)" << std::endl;
    }

    void write_json() {
        std::string filename = "universe." + chan_ + "." + date_ + "." + timestamp_ + ".json.gz";
        gzFile out = gzopen(filename.c_str(), "wb");
        if (!out) {
            throw std::runtime_error("Failed to open JSON file for writing: " + filename);
        }

        json j = {
            {"channel", chan_},
            {"date", date_},
            {"timestamp", timestamp_},
            {"instruments", json::array()}
        };

        for (const auto& [secID, entry] : entries_) {
            j["instruments"].push_back(entry.full_message);
        }

        std::string json_str = j.dump(2);  // Pretty print with 2-space indent
        gzwrite(out, json_str.c_str(), json_str.length());

        gzclose(out);
        std::cerr << "Wrote " << filename << " (" << entries_.size() << " instruments)" << std::endl;
    }
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <binfile>\n";
        std::cerr << "Example: " << argv[0] << " data.310.20260319.1234567890.bin.gz\n";
        return 1;
    }

    std::string bin_file = argv[1];

    if (!fs::exists(bin_file)) {
        std::cerr << "ERROR: File not found: " << bin_file << std::endl;
        return 1;
    }

    try {
        UniverseBuilder builder(bin_file);
        builder.scan();
        builder.write_csv();
        builder.write_json();

        std::cerr << "Universe extraction complete!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
