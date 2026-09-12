/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// merge_bins — k-way merge two gzipped l3-format .bin files into one
// timestamp-sorted output.
//
// Purpose: sim/BFA takes ONE data file per invocation. To run a
// spread strategy (long NQ, short ES) in sim, both chan-310 (ES) and
// chan-318 (NQ) ticks must arrive in one interleaved replay stream.
// This tool merges two per-channel .bin files by transactTime.
//
// Preserves records with ts=0 (SOM/INTERVAL/QLEN/FDF/ODF metadata) in
// their source-stream order; MBO/MBP/trade records are strict-sorted
// by transactTime, ties broken toward stream 1 (--in1).
//
// Usage:
//   merge_bins --in1 310.YYYYMMDD.databento.bin \
//              --in2 318.YYYYMMDD.databento.bin \
//              --out merged.YYYYMMDD.bin
//
// See backtest_agent/docs/pr_161_kaspr_sim_test_plan_2026-07-04.md
// §Gap 1 for the motivating problem.

#include "chutil/Macros.hpp"

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <iostream>
#include <string>
#include <cstdint>

#include "bfile/r_l3.hpp"
#include "zlib.h"

namespace po = boost::program_options;
using namespace std;
using namespace bfile;

int main(int argc, char** argv)
{
    po::options_description desc("merge_bins options");
    desc.add_options()
        ("help,h", "show help")
        ("in1", po::value<string>()->required(),
                "input .bin file 1 (e.g. 310.YYYYMMDD.databento.bin)")
        ("in2", po::value<string>()->required(),
                "input .bin file 2 (e.g. 318.YYYYMMDD.databento.bin)")
        ("out", po::value<string>()->required(),
                "output merged .bin file (gzip compressed)")
        ("verbose,v", "print per-record counts on completion")
        ;

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help") || argc == 1) {
            cout << desc << endl;
            return 1;
        }
        po::notify(vm);
    } catch (const po::error& e) {
        cerr << "arg error: " << e.what() << endl << desc << endl;
        return 2;
    }

    const string in1 = vm["in1"].as<string>();
    const string in2 = vm["in2"].as<string>();
    const string out = vm["out"].as<string>();
    const bool   verbose = vm.count("verbose") > 0;

    gzFile f1 = gzopen(in1.c_str(), "rb");
    if (!f1) { cerr << "cannot open --in1: " << in1 << endl; return 3; }
    gzFile f2 = gzopen(in2.c_str(), "rb");
    if (!f2) { cerr << "cannot open --in2: " << in2 << endl; gzclose(f1); return 3; }
    gzFile fout = gzopen(out.c_str(), "wb");
    if (!fout) { cerr << "cannot open --out: " << out << endl; gzclose(f1); gzclose(f2); return 3; }

    l3_t r1, r2;
    uint64_t ts1 = 0, ts2 = 0;
    bool have1 = read_l3(f1, r1, ts1);
    bool have2 = read_l3(f2, r2, ts2);

    uint64_t written_from_1 = 0, written_from_2 = 0;
    uint64_t last_ts = 0;

    // k=2 merge. Records with ts=0 (SOM/INTERVAL/QLEN and other
    // metadata) get drained from whichever stream currently has one
    // pending; this preserves the intended "metadata first" ordering
    // within each stream as long as each source file has its own FDFs
    // at the head of the file (which the databento generator does).
    //
    // The strict `<=` bias toward stream 1 on ties keeps the output
    // deterministic even when a chan-310 and chan-318 event share a
    // wall-clock ns.
    while (have1 && have2) {
        if (ts1 <= ts2) {
            write_l3(fout, r1);
            ++written_from_1;
            last_ts = std::max(last_ts, ts1);
            have1 = read_l3(f1, r1, ts1);
        } else {
            write_l3(fout, r2);
            ++written_from_2;
            last_ts = std::max(last_ts, ts2);
            have2 = read_l3(f2, r2, ts2);
        }
    }
    while (have1) {
        write_l3(fout, r1);
        ++written_from_1;
        last_ts = std::max(last_ts, ts1);
        have1 = read_l3(f1, r1, ts1);
    }
    while (have2) {
        write_l3(fout, r2);
        ++written_from_2;
        last_ts = std::max(last_ts, ts2);
        have2 = read_l3(f2, r2, ts2);
    }

    gzclose(f1);
    gzclose(f2);
    gzclose(fout);

    if (verbose) {
        cout << "merged: from_in1=" << written_from_1
             << " from_in2=" << written_from_2
             << " total=" << (written_from_1 + written_from_2)
             << " last_ts=" << last_ts
             << endl;
    }
    return 0;
}
