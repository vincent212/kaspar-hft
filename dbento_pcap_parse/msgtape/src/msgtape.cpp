/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * msgtape — dump per-message arrival tape for a securityID from a .bin.
 *
 * For each MBO and MBO-trade record matching the given securityID, emit one
 * CSV row of arrival-process fields:
 *
 *   transactTime, sendingTime, handlerendtim, recv_time,
 *   packet_seq, idx_in_packet,
 *   typ, action, side, pxd, sz, orderID
 *
 * typ    — 'M' for MBO order event, 'T' for MBO-trade
 * action — orderUpdateAction (0=New, 1=Change, 2=Delete, ...) for MBO; 'X' for trade
 * recv_time — pcap kernel timestamp (present on trades; blank on MBO)
 * packet_seq   — 0-indexed count of unique UDP packets observed in the .bin, by
 *                the time this record was decoded (assigned across ALL records,
 *                not just those matching the secID filter). Detected by change
 *                in handlerendtim (the decoder writes handlerendtim once per
 *                packet: it is the kernel-time at which decoding of that packet
 *                finished, so it is constant within a packet and monotone
 *                across packets).
 * idx_in_packet — 0-indexed position of this record within its UDP packet.
 *                Holes are expected after the secID filter: if a packet had
 *                8 messages and 3 matched, idx values could be 1, 4, 7.
 *
 * All timestamps are ns since epoch. Downstream Python does the stats.
 *
 * Usage:
 *   msgtape --datafile 318.20250310.databento.bin --secid 42288528 --out NQH5.csv
 *   msgtape --datafile ... --secid 42288528 --rth-only --out NQH5.rth.csv
 */

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include <boost/program_options.hpp>

#include "bfile/r_l3.hpp"
#include "chutil/Macros.hpp"

namespace po = boost::program_options;

namespace {

// ns since Unix epoch → wall-clock hour*100 + minute in ET (UTC−4 during EDT,
// UTC−5 during EST). We use the timezone of the machine at *record time*, i.e.
// let localtime_r fill in the conversion after setting TZ=America/New_York
// via getenv. Simpler: we just check whether the second-of-day falls in the
// RTH window under the assumption that TZ is already set for the process.
inline int hm_et_from_ns(uint64_t ts_ns)
{
    time_t t = (time_t)(ts_ns / 1'000'000'000ULL);
    struct tm tm_et;
    localtime_r(&t, &tm_et);
    return tm_et.tm_hour * 100 + tm_et.tm_min;
}

bool in_rth(uint64_t ts_ns)
{
    int hm = hm_et_from_ns(ts_ns);
    return hm >= 930 && hm < 1600;
}

} // namespace

int main(int argc, char* argv[])
{
    SET_ARGS;

    // --rth-only reads localtime_r(); make sure TZ is picked up. If TZ isn't
    // in the environment, force America/New_York so the CME RTH window (9:30
    // - 16:00 ET) is honoured. Without this the tape silently drops the first
    // ~5 hours of RTH under a UTC-default server.
    if (getenv("TZ") == nullptr) {
        setenv("TZ", "America/New_York", 1);
    }
    tzset();

    po::options_description desc("msgtape options");
    desc.add_options()
        ("help",     "produce help message")
        ("datafile", po::value<std::string>(),
                     "gzip'd L3 .bin to scan (required)")
        ("secid",    po::value<int32_t>(),
                     "restrict to this securityID (required)")
        ("out",      po::value<std::string>(),
                     "output CSV path; if omitted, writes to stdout")
        ("rth-only", po::bool_switch(),
                     "keep only messages with transactTime inside RTH "
                     "(09:30-16:00 ET; requires TZ=America/New_York)")
        ("include-mbo",   po::value<bool>()->default_value(true),
                          "include MBO order events (default true)")
        ("include-trade", po::value<bool>()->default_value(true),
                          "include MBO-trade events (default true)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help") || !vm.count("datafile") || !vm.count("secid")) {
        std::cout << desc << "\n";
        return 1;
    }

    const auto file  = vm["datafile"].as<std::string>();
    const auto secid = vm["secid"].as<int32_t>();
    const bool rth   = vm["rth-only"].as<bool>();
    const bool w_mbo = vm["include-mbo"].as<bool>();
    const bool w_trd = vm["include-trade"].as<bool>();

    std::ofstream fout;
    std::ostream* out = &std::cout;
    if (vm.count("out")) {
        fout.open(vm["out"].as<std::string>());
        if (!fout) { std::cerr << "cannot open output\n"; return 2; }
        out = &fout;
    }

    // For MBO records we need to map an incoming trade to the last-seen
    // securityID via the maker orderID, since MBO-trade doesn't carry
    // securityID directly — it carries only the orderID.
    // Rebuild a running orderID→securityID map from MBO Add events.
    // Memory: for a full CME futures day this is ~10^7 entries × 12 bytes
    // ~120 MB — acceptable.
    std::unordered_map<uint64_t, int32_t> oid2sec;
    oid2sec.reserve(1 << 24);

    // CSV header
    (*out) << "transactTime,sendingTime,handlerendtim,recv_time,"
              "packet_seq,idx_in_packet,"
              "typ,action,side,pxd,sz,orderID\n";

    gzFile f = gzopen(file.c_str(), "rb");
    if (!f) { std::cerr << "cannot open " << file << "\n"; return 3; }

    bfile::l3_t l3;
    uint64_t ts = 0;
    uint64_t n_mbo = 0, n_trd = 0, n_kept_mbo = 0, n_kept_trd = 0;

    // Packet boundary tracking: handlerendtim is set once per UDP packet by the
    // decoder (it is the kernel time at which decoding of that packet completed).
    // Two consecutive records with the same handlerendtim came from the same
    // packet; a change signals the start of a new packet.
    //
    // Packet indices are assigned across ALL records seen in the .bin (not just
    // ones matching --secid), so packet_seq is a stable wire-order index. That
    // means after filtering, idx_in_packet can have holes — e.g. idx = 1, 4, 7
    // if a packet held 8 messages and 3 matched.
    uint64_t prev_handlerendtim = 0;
    uint64_t packet_seq = 0;         // 0-indexed; first packet is 0
    uint32_t idx_in_packet = 0;
    bool have_seen_packet = false;

    auto advance_packet_index = [&](uint64_t handlerendtim) {
        if (!have_seen_packet) {
            have_seen_packet = true;
            prev_handlerendtim = handlerendtim;
            idx_in_packet = 0;
            packet_seq = 0;
            return;
        }
        if (handlerendtim != prev_handlerendtim) {
            ++packet_seq;
            idx_in_packet = 0;
            prev_handlerendtim = handlerendtim;
        } else {
            ++idx_in_packet;
        }
    };

    char buf[256];
    while (bfile::read_l3(f, l3, ts)) {
        if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) {
            const auto& m = std::get<bfile::l3_mbo_v2_t>(l3);
            ++n_mbo;
            advance_packet_index(m.handlerendtim);

            // Track orderID→securityID for downstream trade attribution.
            // Add: bind. Delete: erase after emit. Change: leave bound.
            if (m.orderUpdateAction == 0 /*Add*/) {
                oid2sec[m.orderID] = m.securityID;
            }

            if (m.securityID != secid) continue;
            if (rth && !in_rth(m.transactTime)) continue;
            if (!w_mbo) continue;
            ++n_kept_mbo;
            int nchar = std::snprintf(buf, sizeof(buf),
                "%lu,%lu,%lu,,%lu,%u,M,%u,%c,%.9f,%u,%lu\n",
                (unsigned long)m.transactTime,
                (unsigned long)m.sendingTime,
                (unsigned long)m.handlerendtim,
                (unsigned long)packet_seq,
                (unsigned)idx_in_packet,
                (unsigned)m.orderUpdateAction,
                m.side,
                m.pxd,
                (unsigned)m.displayQty,
                (unsigned long)m.orderID);
            out->write(buf, nchar);

            if (m.orderUpdateAction == 2 /*Delete*/) {
                oid2sec.erase(m.orderID);
            }
        } else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3)) {
            const auto& t = std::get<bfile::l3_mbo_trd_v2_t>(l3);
            ++n_trd;
            advance_packet_index(t.handlerendtim);

            auto it = oid2sec.find(t.orderID);
            if (it == oid2sec.end()) continue;   // maker not seen (recovery/history)
            if (it->second != secid) continue;
            if (rth && !in_rth(t.transactTime)) continue;
            if (!w_trd) continue;
            ++n_kept_trd;
            int nchar = std::snprintf(buf, sizeof(buf),
                "%lu,%lu,%lu,%lu,%lu,%u,T,X,,,%d,%lu\n",
                (unsigned long)t.transactTime,
                (unsigned long)t.sendingTime,
                (unsigned long)t.handlerendtim,
                (unsigned long)t.recv_time,
                (unsigned long)packet_seq,
                (unsigned)idx_in_packet,
                (int)t.lastQty,
                (unsigned long)t.orderID);
            out->write(buf, nchar);
        }
    }
    gzclose(f);

    std::cerr << "msgtape: read " << n_mbo << " MBO + " << n_trd
              << " MBO-trade records; kept " << n_kept_mbo << " MBO + "
              << n_kept_trd << " trade for secID=" << secid
              << (rth ? " (RTH filter)" : "") << "\n";
    return 0;
}
