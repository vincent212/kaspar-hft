/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

#include <pcap/pcap.h>
#include <arpa/inet.h>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

// Shell-quote a filename so it can be safely embedded in a popen() command.
// Wraps in single quotes and escapes any internal single quotes via the
// standard ' → '"'"' idiom. Necessary because popen runs the command
// through /bin/sh and a filename containing ' would otherwise break out.
static std::string shell_quote_single(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else           out += c;
    }
    out += "'";
    return out;
}

struct pkt_key {
    uint32_t src_ip;
    uint16_t dst_port;
    bool operator<(const pkt_key& o) const {
        if (src_ip != o.src_ip) return src_ip < o.src_ip;
        return dst_port < o.dst_port;
    }
};

static pkt_key extract_key(const u_char* packet, uint32_t caplen)
{
    pkt_key k{0, 0};
    if (caplen < 14) return k;

    uint16_t ethertype;
    memcpy(&ethertype, packet + 12, 2);
    ethertype = ntohs(ethertype);
    uint32_t offset = 14;
    if (ethertype == 0x8100) {
        if (caplen < 18) return k;
        memcpy(&ethertype, packet + 16, 2);
        ethertype = ntohs(ethertype);
        offset = 18;
    }
    if (ethertype != 0x0800) return k;
    if (caplen < offset + 20) return k;

    // Reject non-UDP (proto != 17) — TCP/ICMP/etc. don't have our "dst port"
    // in bytes 2-3 of the L4 header, so reporting them would be bogus.
    if (packet[offset + 9] != 17) return k;

    memcpy(&k.src_ip, packet + offset + 12, 4);

    uint8_t ihl = (packet[offset] & 0x0F);
    offset += ihl * 4;

    if (caplen < offset + 8) return k;
    uint16_t dst_port;
    memcpy(&dst_port, packet + offset + 2, 2);  // UDP dst port at byte 2 of UDP header
    k.dst_port = ntohs(dst_port);

    return k;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <pcap-dir>\n";
        return 1;
    }

    std::string pcap_dir = argv[1];
    if (!fs::exists(pcap_dir) || !fs::is_directory(pcap_dir)) {
        std::cerr << "ERROR: Not a directory: " << pcap_dir << "\n";
        return 1;
    }

    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(pcap_dir)) {
        if (fs::is_regular_file(e) && e.path().string().find(".pcap") != std::string::npos)
            files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    std::cout << files.size() << " files in " << pcap_dir << "\n";

    std::map<pkt_key, uint64_t> counts;

    for (const auto& f : files) {
        char errbuf[PCAP_ERRBUF_SIZE];
        pcap_t* pcap = nullptr;

        std::string fn = f;
        if (fn.size() >= 4 && fn.substr(fn.size() - 4) == ".zst") {
            std::string cmd = "zstdcat " + shell_quote_single(fn);
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) { std::cerr << "popen failed: " << fn << "\n"; continue; }
            pcap = pcap_fopen_offline(pipe, errbuf);
        } else {
            pcap = pcap_open_offline(fn.c_str(), errbuf);
        }
        if (!pcap) { std::cerr << "pcap open failed: " << fn << ": " << errbuf << "\n"; continue; }

        pcap_pkthdr* hdr;
        const u_char* pkt;
        while (pcap_next_ex(pcap, &hdr, &pkt) == 1) {
            pkt_key k = extract_key(pkt, hdr->caplen);
            // Skip non-UDP / malformed — extract_key returns {0,0} for those.
            if (k.src_ip == 0 && k.dst_port == 0) continue;
            counts[k]++;
        }

        pcap_close(pcap);
        std::cout << "  " << fs::path(f).filename().string() << " done\n";
    }

    std::cout << "\n=== Source IP : Dst Port  =>  Packets ===\n";
    for (const auto& [k, cnt] : counts) {
        struct in_addr a; a.s_addr = k.src_ip;
        std::cout << inet_ntoa(a) << ":" << k.dst_port << "  " << cnt << " packets\n";
    }

    return 0;
}
