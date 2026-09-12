/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <boost/thread/thread.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/bind.hpp>

#include "logger/act/Logger.hpp"
#include "dbento_pcap_to_bin.hpp"
#include "Mdp3InfoParser.hpp"

namespace po = boost::program_options;
using namespace std;

#include "chutil/sig_hand.hpp"

#include <fenv.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    SET_ARGS;
    PRINT_ARGS;
    tzset();

    ::signal(SIGSEGV, &my_signal_handler);
    ::signal(SIGABRT, &my_signal_handler);
    ::signal(SIGBUS, &my_signal_handler);

    cout << argv[0] << endl;
    cout << boost::filesystem::current_path() << endl;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help",          "produce help message")
        ("pcap-dir",      po::value<string>(), "directory containing .pcap.zst files")
        ("chan",          po::value<int>(),    "CME channel ID (e.g. 310 for ES)")
        ("color",         po::value<char>()->default_value('a'), "feed color: a or b (default: a)")
        ("format",        po::value<string>()->default_value("per-channel"),
                          "input layout: per-channel (one pcap per mcast group; default) "
                          "or consolidated (all channels muxed per 10-min file)")
        ("allow-partial-day", "consolidated only: suppress the IR-warmup warning when "
                              "the first matched file isn't T000000 (use only if you "
                              "understand that some incremental updates may reference "
                              "SecurityIDs whose definitions haven't been seen yet)")
        ("logdebug",      "enable debug logs")
        ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help") || argc == 1) {
        cout << desc << "\n\nExamples:\n"
             << "  # old per-channel layout (futures-xcme/, options-xcme/)\n"
             << "  ./dbento_pcap_to_bin --pcap-dir /nvs/vendor/databento/pcaps/glbx/futures-xcme/20260119"
             << " --chan 310\n"
             << "  ./dbento_pcap_to_bin --pcap-dir ... --chan 312 --color b\n\n"
             << "  # new consolidated layout (consolidated/<date>/)\n"
             << "  ./dbento_pcap_to_bin --pcap-dir /nvs/vendor/databento/pcaps/glbx/consolidated/20241230"
             << " --chan 310 --format consolidated\n";
        return 1;
    }

    if (!vm.count("pcap-dir")) { cerr << "Error: --pcap-dir required\n"; return 1; }
    if (!vm.count("chan"))      { cerr << "Error: --chan required\n"; return 1; }

    string pcap_dir = vm["pcap-dir"].as<string>();
    int    chanid   = vm["chan"].as<int>();
    char   color    = vm["color"].as<char>();
    string format   = vm["format"].as<string>();

    if (color != 'a' && color != 'b') {
        cerr << "Error: --color must be 'a' or 'b'\n"; return 1;
    }
    bool consolidated = false;
    if      (format == "per-channel")  consolidated = false;
    else if (format == "consolidated") consolidated = true;
    else { cerr << "Error: --format must be 'per-channel' or 'consolidated' (got: " << format << ")\n"; return 1; }
    bool allow_partial_day = vm.count("allow-partial-day") > 0;
    if (allow_partial_day && !consolidated) {
        cerr << "Warning: --allow-partial-day has no effect in per-channel mode (ignored)\n";
    }

    // Resolve config path independent of the caller's CWD. The batch scripts
    // chdir into per-channel output dirs before exec, so "./genconfig/..."
    // would not work. Require KSPRPROJ explicitly.
    const char* ksprproj = getenv("KSPRPROJ");
    if (!ksprproj || !*ksprproj) {
        cerr << "ERROR: KSPRPROJ environment variable must be set (points at the kaspar-hft repo root)\n";
        return 1;
    }
    string info_path = string(ksprproj) + "/genconfig/mdp3_prod.info";

    // Look up both incr_port (feed A or B) and snap_port (IR) from
    // mdp3_prod.info. Using config for both is more robust than computing
    // "14000+chan" — a handful of CME channels (e.g. chan323 with port_a
    // 14346) don't follow that convention. Snap IP is needed because the
    // MBP-snapshot stream and the IR stream share port_ir on different IPs,
    // and only IR carries instrument definitions.
    int    incr_port, snap_port;
    string incr_ip, snap_ip;
    try {
        auto [iip, iport] = mdp3_info_mcast(info_path, chanid, color);
        incr_ip   = iip;   // used as the dst_ip in the consolidated-format filter
        incr_port = iport;
        auto [sip, sport] = mdp3_info_snap(info_path, chanid);
        snap_ip   = sip;
        snap_port = sport;
    } catch (const exception& e) {
        cerr << "ERROR resolving channel " << chanid << " from " << info_path
             << ": " << e.what() << "\n";
        return 1;
    }

    cout << "Channel:    " << chanid << "\n";
    cout << "Feed:       " << color << "\n";
    cout << "Format:     " << format << "\n";
    cout << "Incr:       " << incr_ip << ":" << incr_port << "\n";
    cout << "Snap:       " << snap_ip << ":" << snap_port << "\n";
    cout << "Directory:  " << pcap_dir << "\n";

    if (vm.count("logdebug")) {
        polonaise::logger::act::Logger::synchrolog = true;
        polonaise::logger::act::Logger::log_debug  = true;
    } else {
        polonaise::logger::act::Logger::log_debug  = false;
        polonaise::logger::act::Logger::synchrolog = false;
    }
    polonaise::logger::act::Logger::rt = true;

    dbento_pcap_to_bin* man = nullptr;
    try {
        man = new dbento_pcap_to_bin(chanid, pcap_dir, incr_ip, incr_port,
                                   snap_ip, snap_port, consolidated, color,
                                   allow_partial_day);
    } catch (const std::exception& e) {
        cerr << "ERROR constructing dbento_pcap_to_bin: " << e.what() << "\n";
        return 1;
    }

    feenableexcept(FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INVALID);

    auto thrd = boost::thread(boost::ref(*man));
    thrd.join();

    // PcapFileManager sets failed() on any scan error or unexpected actor
    // dropout. Propagate that to the process exit code so batch scripts
    // treat aborted runs as failures instead of mistaking a partial .bin
    // for a successful capture.
    if (man->file_manager && man->file_manager->failed()) {
        cerr << "ERROR: run aborted by PcapFileManager\n";
        return 1;
    }
    return 0;
}
