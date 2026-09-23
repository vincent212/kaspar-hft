#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <regex>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <utility>

#include <arpa/inet.h>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

//
// Actors framework (NOT cfsm)
//
#include "actors/act/Manager.hpp"
#include "actors/act/Group.hpp"

//
// Frame and logging
//
#include "logger/act/Logger.hpp"
#include "frame/mda/if/BinRecorder.hpp"

//
// MDP3 market data
//
#include "mdp3/if/mdp3.hpp"
#include "mdp3/handler_if.hpp"
#include "mdp3/DataDecoder.hpp"

//
// MCAST (for PCAPReader)
//
#include "mcast_recv/act/PCAPReader.hpp"
#include "mcast_recv/act/MsgBuf.hpp"
#include "mcast_recv/if/PCAPReader.hpp"  // for create_PCAPReader_32_0

//
// File manager
//
#include "PcapFileManager.hpp"

#include "chutil/Time.hpp"

struct dbento_pcap_to_bin : public actors::Manager
{
    actors::Actor*    binrec;
    actors::Actor*    log;
    actors::Actor*    msg_buf;
    actors::Actor*    message_processor;
    PcapFileManager*  file_manager;  // concrete type so main() can read failed()
    actors::Group*    group;
    handler_if<> handler_treas;
    int chan;

    // Pull YYYYMMDD from the pcap-dir basename when it matches that shape
    // (Databento ships .../consolidated/<date>/, .../futures-xcme/<date>/),
    // otherwise scan the first matching file for "-20\d\d\d\d\d\d" and
    // extract that. Returns "unknown" if neither finds a date.
    static std::string derive_date_str(const std::string& pcap_dir)
    {
        namespace fs = boost::filesystem;
        const std::regex date_re(R"(^\d{8}$)");
        // Accept any non-alnum separator after the YYYYMMDD so older naming
        // layouts (which used '_', '-', or '.') and 2010s-era pre-2020
        // archives still parse. Anchored on a leading '-' to avoid picking
        // up dates embedded in random tokens.
        const std::regex file_date_re(R"(-((?:19|20)\d{6})(?:[^0-9]|$))");

        // 1) Directory basename: prefer the canonical Databento layout.
        std::string basename = fs::path(pcap_dir).filename().string();
        if (std::regex_match(basename, date_re)) {
            std::cout << "  Date from directory basename: " << basename << std::endl;
            return basename;
        }

        // 2) Fallback: scan files for the first "-YYYYMMDD<sep>" embedded date.
        if (fs::exists(pcap_dir) && fs::is_directory(pcap_dir)) {
            std::vector<std::string> names;
            for (const auto& entry : fs::directory_iterator(pcap_dir)) {
                if (fs::is_regular_file(entry) &&
                    entry.path().filename().string().find(".pcap") != std::string::npos) {
                    names.push_back(entry.path().filename().string());
                }
            }
            std::sort(names.begin(), names.end());
            for (const auto& n : names) {
                std::smatch m;
                if (std::regex_search(n, m, file_date_re)) {
                    std::cout << "  Date from first matching file (" << n << "): "
                              << m[1].str() << std::endl;
                    return m[1].str();
                }
            }
        }
        // Fail fast instead of returning the literal "unknown": silently
        // producing `<chan>.unknown.databento.bin` lets batch scripts mistake
        // a misconfigured run for success. Force the operator to pass a
        // properly-named directory.
        throw std::runtime_error(
            "dbento_pcap_to_bin: could not derive YYYYMMDD from pcap-dir basename "
            "'" + basename + "' or any file therein. Expected the directory to "
            "be named YYYYMMDD (Databento default), or for files to embed "
            "'-YYYYMMDD<sep>' (any non-alnum separator after the date).");
    }

    // PER_CHANNEL: pass only ports; PCAPReader trusts the filename for chan
    // selection. CONSOLIDATED: pass `consolidated=true` AND the full set of
    // multicast endpoints — every packet is filtered by (dst_ip, dst_port)
    // inside PCAPReader. `color` and `allow_partial_day` are CONSOLIDATED-only
    // (PER_CHANNEL ignores them).
    dbento_pcap_to_bin(
        int chan,
        const std::string &pcap_dir,
        const std::string &incr_ip,
        int incr_port,
        const std::string &snap_ip,
        int snap_port,
        bool consolidated = false,
        char color = 'a',
        bool allow_partial_day = false)
        : handler_treas(en::x::CMEMD, chan, true),
          chan(chan)
    {
        // Range-check port numbers BEFORE we narrow them to uint16_t in the
        // filter. Mdp3InfoParser returns int with only a !=0 check, so a
        // mistyped config like `port_a 144310` (extra digit) would otherwise
        // silently wrap to 13174 and every packet would be filter-dropped.
        if (incr_port < 1 || incr_port > 65535) {
            throw std::runtime_error("dbento_pcap_to_bin: incr_port out of UDP range (1..65535): "
                                     + std::to_string(incr_port));
        }
        if (snap_port < 1 || snap_port > 65535) {
            throw std::runtime_error("dbento_pcap_to_bin: snap_port out of UDP range (1..65535): "
                                     + std::to_string(snap_port));
        }

        std::cout << "Databento PCAP to M2 converter" << std::endl;
        std::cout << "  Channel:    " << chan << std::endl;
        std::cout << "  Format:     " << (consolidated ? "consolidated" : "per-channel") << std::endl;
        std::cout << "  Incr:       " << incr_ip << ":" << incr_port
                  << (consolidated ? " (filter: any IP, port-only)" : " (filter: filename suffix)")
                  << std::endl;
        std::cout << "  Snap:       " << snap_ip << ":" << snap_port << std::endl;
        std::cout << "  Directory:  " << pcap_dir << std::endl;

        auto chanstr = boost::lexical_cast<std::string>(chan);

        // Date for output filename: <chan>.<YYYYMMDD>.databento.bin.
        // Prefer the directory basename (Databento ships <date>/ subdirs:
        // .../consolidated/20241230/, .../futures-xcme/20260227/), which is
        // unambiguous and doesn't depend on per-file naming. Fall back to
        // first-file scan when the basename doesn't look like YYYYMMDD, so
        // operators pointing at an unusual layout still get something.
        std::string date_str = derive_date_str(pcap_dir);

        // Create output filename: chan.YYYYMMDD.databento.bin
        auto recfname = chanstr + "." + date_str + ".databento.bin";

        std::cout << "Output file: " << recfname << std::endl;

        binrec = create_BinRecorder(recfname);

        // Setup CME MDP3 handler
        handler_treas.binrec = binrec;

        // Build the decode actor inline-only: no worker fleet is wired here (no
        // set_workers), so parallel_decode_ stays off and every packet decodes
        // inline via mbo_data -- exactly as before DataDecoder became an actor.
        // MBO enabled, max 10 MBP levels.
        auto *decoder = new mdp3::DataDecoder(&handler_treas, false, 10, false);

        // Create persistent MessageProcessor and MsgBuf (shared across all files)
        message_processor = create_MessageProcessor(
            chanstr,
            nullptr,  // No recovery in PCAP mode
            decoder,
            false,    // no recovery
            false);   // no recovery on start

        msg_buf = create_MsgBuf_32(
            chanstr,
            0,        // spinbuf
            'A',
            message_processor);

        // Create Group for serialized execution with persistent actors
        group = new actors::Group("dbento_pcap_group");
        group->add(msg_buf);
        group->add(message_processor);
        group->add(decoder);
        group->add(binrec);

        // Create PcapFileManager (separate actor, NOT in group)
        // It will scan the directory and orchestrate processing of all PCAP files via message passing
        if (consolidated) {
            // Build the per-packet filter from the resolved endpoints.
            // - incremental: STRICT (incr_ip, incr_port). A wildcard dst_ip=0
            //   for incr_port would short-circuit the strict IR-only filter
            //   below whenever incr_port == port_ir (true for every CME
            //   channel today: port_ir = 14000+chan = port_a), letting
            //   MBP-snapshot packets that happen to land on the same port
            //   leak into the IR decode path. If CME re-allocates the
            //   multicast IP for a channel, update mdp3_prod.info — that
            //   config file is the single source of truth for endpoints.
            // - snap/IR: STRICT (snap_ip, snap_port). The IR group shares its
            //   port with the MBP-snapshot stream on a different IP — we want
            //   IR (instrument defs), not snap.
            std::vector<std::pair<uint32_t, uint16_t>> filter;

            in_addr ia{};
            int rci = inet_pton(AF_INET, incr_ip.c_str(), &ia);
            if (rci != 1) {
                throw std::runtime_error(
                    "dbento_pcap_to_bin: inet_pton failed on incr_ip '" + incr_ip +
                    "' (rc=" + std::to_string(rci) + "). Refusing to start: a "
                    "wildcard incremental filter would let MBP-snap packets "
                    "into the IR decode path when port_a == port_ir.");
            }
            // Explicitly reject 0.0.0.0 — inet_pton accepts it (s_addr == 0)
            // but 0 is the wildcard sentinel inside PCAPReader, so the filter
            // entry would collapse to "match any IP on this port" — exactly
            // the failure mode this strict-match was meant to prevent.
            if (ia.s_addr == 0) {
                throw std::runtime_error(
                    "dbento_pcap_to_bin: incr_ip resolved to 0.0.0.0 — "
                    "this is the wildcard sentinel in PCAPReader and would "
                    "admit MBP-snap packets when port_a == port_ir. "
                    "Check mdp3_prod.info for chan " +
                    std::to_string(chan) + ".");
            }
            filter.push_back({ia.s_addr, static_cast<uint16_t>(incr_port)});

            in_addr sa{};
            int rc = inet_pton(AF_INET, snap_ip.c_str(), &sa);
            if (rc != 1) {
                throw std::runtime_error(
                    "dbento_pcap_to_bin: inet_pton failed on snap_ip '" + snap_ip +
                    "' (rc=" + std::to_string(rc) + "). Refusing to start: a "
                    "wildcard snap filter would admit MBP-snap packets into the "
                    "IR decode path and corrupt instrument definitions.");
            }
            filter.push_back({sa.s_addr, static_cast<uint16_t>(snap_port)});

            file_manager = new PcapFileManager(group, msg_buf, pcap_dir, chanstr,
                                               filter, color, allow_partial_day, this);
        } else {
            file_manager = new PcapFileManager(group, msg_buf, pcap_dir, chanstr,
                                               incr_port, snap_ip, snap_port, this);
        }

        // Logger (managed separately for early logging)
        log = new polonaise::logger::act::Logger("log.databento." + chanstr);

        // Manage all actors
        add_to_manage_q(group);
        add_to_manage_q(file_manager);
        add_to_manage_q(log);

        // Note: Do NOT call init() here!
        // The Manager thread (started by boost::thread in main.cpp) will call
        // Actor::operator()() which calls init() automatically.
        // Calling init() twice causes double-start and double-free errors!

        // Constructor returns immediately - file processing happens asynchronously!
        std::cout << "dbento_pcap_to_bin initialized" << std::endl;
        std::cout << "Output file: " << recfname << std::endl;
    }
};
