#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

struct Mdp3ChanInfo {
    std::string group_a, group_b, group_ir, group_dr;
    int port_a = 0, port_b = 0, port_ir = 0, port_dr = 0;
};

// Parse mdp3_prod.info and return channel info for the given chanid.
inline Mdp3ChanInfo mdp3_info_lookup(const std::string& info_path, int chanid)
{
    std::ifstream f(info_path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open: " + info_path);

    Mdp3ChanInfo current;
    int current_chanid = -1;
    bool in_block = false;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key;
        // Consume every whitespace-separated key/value pair on the line —
        // some config variants pack multiple pairs per line.
        while (ss >> key) {
            if (key == "chanid")       { ss >> current_chanid; in_block = true; }
            else if (key == "group_a") { ss >> current.group_a; }
            else if (key == "group_b") { ss >> current.group_b; }
            else if (key == "group_ir"){ ss >> current.group_ir; }
            else if (key == "group_dr"){ ss >> current.group_dr; }
            else if (key == "port_a")  { ss >> current.port_a; }
            else if (key == "port_b")  { ss >> current.port_b; }
            else if (key == "port_ir") { ss >> current.port_ir; }
            else if (key == "port_dr") { ss >> current.port_dr; }
            else if (key == "}") {
                if (in_block && current_chanid == chanid)
                    return current;
                current = Mdp3ChanInfo{};
                current_chanid = -1;
                in_block = false;
            }
        }
    }

    throw std::runtime_error("Channel " + std::to_string(chanid) + " not found in " + info_path);
}

// Returns the multicast IP and port for the given chanid and color ('a' or 'b').
// Throws if the endpoint is missing/empty, so callers don't silently get
// "" or 0 (which would turn filename filters into catch-alls or no-matches).
inline std::pair<std::string, int> mdp3_info_mcast(const std::string& info_path,
                                                     int chanid, char color)
{
    auto info = mdp3_info_lookup(info_path, chanid);
    std::string ip;
    int port;
    const char* which;
    if (color == 'b' || color == 'B') { ip = info.group_b; port = info.port_b; which = "group_b/port_b"; }
    else                               { ip = info.group_a; port = info.port_a; which = "group_a/port_a"; }
    if (ip.empty() || port == 0) {
        throw std::runtime_error(std::string("chan ") + std::to_string(chanid)
            + " is missing " + which + " in " + info_path);
    }
    return {ip, port};
}

// Returns the incremental-recovery (snap) IP and port.
inline std::pair<std::string, int> mdp3_info_snap(const std::string& info_path, int chanid)
{
    auto info = mdp3_info_lookup(info_path, chanid);
    if (info.group_ir.empty() || info.port_ir == 0) {
        throw std::runtime_error(std::string("chan ") + std::to_string(chanid)
            + " is missing group_ir/port_ir in " + info_path);
    }
    return {info.group_ir, info.port_ir};
}
