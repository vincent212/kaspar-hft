/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// verify_merged — walk a merged .bin file, check ts monotonicity.
// Reports total records, ts=0 metadata count, and any out-of-order pair
// (where ts_curr < ts_prev with both > 0). Used to verify merge_bins
// actually produces a timestamp-sorted stream.

#include "chutil/Macros.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include "bfile/r_l3.hpp"
#include "zlib.h"

namespace po = boost::program_options;
using namespace std;
using namespace bfile;

int main(int argc, char** argv) {
    po::options_description desc("verify_merged");
    desc.add_options()
        ("help,h", "help")
        ("in", po::value<string>()->required(), "merged .bin file")
        ("show-violations", po::value<int>()->default_value(10),
            "print up to N out-of-order pairs")
        ;
    po::variables_map vm;
    try { po::store(po::parse_command_line(argc, argv, desc), vm);
          if (vm.count("help") || argc == 1) { cout << desc << endl; return 1; }
          po::notify(vm); }
    catch (const po::error& e) { cerr << e.what() << endl; return 2; }

    const string in = vm["in"].as<string>();
    const int max_show = vm["show-violations"].as<int>();

    gzFile f = gzopen(in.c_str(), "rb");
    if (!f) { cerr << "cannot open: " << in << endl; return 3; }

    uint64_t total = 0, meta = 0, ts_records = 0, violations = 0;
    uint64_t last_ts = 0;
    int shown = 0;

    l3_t l3;
    uint64_t ts;
    while (read_l3(f, l3, ts)) {
        ++total;
        if (ts == 0) {
            ++meta;
        } else {
            ++ts_records;
            if (last_ts > 0 && ts < last_ts) {
                ++violations;
                if (shown < max_show) {
                    cerr << "  violation @ record " << total
                         << ": ts=" << ts << " < prev=" << last_ts
                         << " (diff=" << (int64_t)(ts - last_ts) << ")" << endl;
                    ++shown;
                }
            }
            last_ts = ts;
        }
    }
    gzclose(f);

    cout << "file:          " << in << endl;
    cout << "total records: " << total << endl;
    cout << "ts>0 records:  " << ts_records << endl;
    cout << "ts=0 metadata: " << meta << endl;
    cout << "violations:    " << violations
         << ((violations == 0) ? "  ✓ monotonic" : "  ✗ NOT monotonic")
         << endl;
    cout << "last_ts:       " << last_ts << endl;
    return violations == 0 ? 0 : 1;
}
