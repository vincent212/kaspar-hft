#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include <limits>
#include <vector>
#include <utility>
#include <arpa/inet.h>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/ShutdownThisActor.hpp"
#include "actors/msg/Continue.hpp"
#include "mcast_recv/message_buffer.hpp"
#include "mcast_recv/trailer.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "logger/act/Logger.hpp"

#include <pcap/pcap.h>
#include <boost/format.hpp>
#include <boost/endian/conversion.hpp>

namespace mcast_recv
{

  template <typename seqnumT, uint8_t N>
  class PCAPReader : public actors::Actor
  {
  private:
    const char* get_name() const { return name; }
    char name[256];
    std::string chan_nam;

    actors::Actor *msg_processor;
    std::string pcap_file;
    const uint8_t seq_num_offset = N; // for CME MDP3 its 0
    bool big_endian;
    TrailerSpec trailer_spec;      // hardware-timestamp trailer layout (default: None)
    bool trailer_validated = false; // first-packet config-vs-wire sanity check done

    // Optional UDP (dst_ip, dst_port) allowlist. Empty = no filtering (the
    // per-channel Databento layout already pre-filters at the file-naming
    // level). Non-empty = drop packets whose (dst_ip, dst_port) doesn't
    // match — used for the "consolidated" layout where every multicast group
    // is muxed into one file.
    //   dst_ip   — NETWORK byte order uint32_t (raw bytes from the IP header
    //              or inet_pton). 0 means "match any IP for this port".
    //   dst_port — HOST byte order uint16_t. The match site ntohs's the
    //              packet's port first, so callers should NOT htons() values
    //              they pass in.
    struct PktFilter { uint32_t dst_ip; uint16_t dst_port; };
    std::vector<PktFilter> pkt_filter;
    uint64_t filtered_drop_count = 0;

    pcap_t *pcap = nullptr;
    uint64_t packet_count = 0;
    uint32_t last_mdp3_seqnum = 0;

    // Wrap a filename in single quotes for safe embedding in a popen()
    // command line. Internal single quotes are escaped via the standard
    // POSIX idiom: ' -> '"'"'. Without this, a filename like
    //   foo'; rm -rf /tmp/bar; echo 'baz.pcap.zst
    // would break out of the quoting in /bin/sh.
    static std::string shell_quote_single(const std::string& s) {
      std::string out = "'";
      for (char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else           out += c;
      }
      out += "'";
      return out;
    }

    // Check if string ends with suffix
    bool ends_with(const std::string& str, const std::string& suffix) {
      if (suffix.size() > str.size()) return false;
      return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void open_pcap()
    {
      char errbuf[PCAP_ERRBUF_SIZE];

      // Check for .zst compressed files
      if (ends_with(pcap_file, ".zst")) {
        // Decompress with zstdcat. pcap_file is shell-quoted because popen
        // runs via /bin/sh; a filename containing ' would otherwise be an
        // injection vector.
        std::string cmd = "zstdcat " + shell_quote_single(pcap_file);
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
          ERRF(boost::format("Failed to open decompression pipe for '%s'") % pcap_file);
        }
        pcap = pcap_fopen_offline(pipe, errbuf);
        if (!pcap) {
          pclose(pipe);
          ERRF(boost::format("Failed to open PCAP from zstdcat pipe '%s': %s") % pcap_file % errbuf);
        }
        // NOTE: pcap now owns the pipe FILE*, will close it with pcap_close()
        log_inf("Opened compressed PCAP file (.zst): %s", pcap_file.c_str());
      }
      else {
        // Regular uncompressed file
        pcap = pcap_open_offline(pcap_file.c_str(), errbuf);
        if (!pcap) {
          ERRF(boost::format("Failed to open PCAP file '%s': %s") % pcap_file % errbuf);
        }
        log_inf("Opened PCAP file: %s", pcap_file.c_str());
      }
    }

    // Returns true if the packet was forwarded to msg_processor, false if it
    // was filter-dropped. Lets continue_handler drain filter-drops inline
    // without a Continue alloc + scheduler round-trip per dropped packet (the
    // consolidated layout rejects >95% per channel).
    bool process_packet(const u_char *packet, uint32_t caplen, [[maybe_unused]] uint32_t orig_len, [[maybe_unused]] uint64_t timestamp_ns)
    {
      // Parse Ethernet/IP/UDP headers to find UDP payload
      // Ethernet: 14 bytes (or 18 with VLAN tag)
      // IP: 20+ bytes (IPv4) or 40 bytes (IPv6)
      // UDP: 8 bytes

      if (caplen < 14) {
        ERRF(boost::format("Packet too small for Ethernet header: %u bytes") % caplen);
      }

      uint32_t offset = 0;

      // Parse Ethernet header
      uint16_t ethertype;
      memcpy(&ethertype, packet + 12, 2);
      ethertype = ntohs(ethertype);
      offset = 14;

      // Handle VLAN tag (802.1Q)
      if (ethertype == 0x8100) {
        if (caplen < 18) {
          ERRF(boost::format("Packet too small for VLAN-tagged Ethernet: %u bytes") % caplen);
        }
        memcpy(&ethertype, packet + 16, 2);
        ethertype = ntohs(ethertype);
        offset = 18;
      }

      // Parse IP header
      uint32_t ip_header_len = 0;
      uint32_t ip_total_len = 0;
      uint32_t src_ip = 0;
      uint32_t dst_ip = 0;
      uint32_t ip_start = offset;

      if (ethertype == 0x0800) {
        // IPv4
        if (caplen < offset + 20) {
          ERRF(boost::format("Packet too small for IPv4 header: %u bytes") % caplen);
        }
        uint8_t ihl = (packet[offset] & 0x0F);
        ip_header_len = ihl * 4;
        uint16_t tot_len_be;
        memcpy(&tot_len_be, packet + ip_start + 2, 2);  // IPv4 Total Length at byte 2, network byte order
        ip_total_len = ntohs(tot_len_be);
        memcpy(&src_ip, packet + ip_start + 12, 4);  // src IP at byte 12 of IPv4 header, network byte order
        memcpy(&dst_ip, packet + ip_start + 16, 4);  // dst IP at byte 16, used by pkt_filter

      } else if (ethertype == 0x86DD) {
        // IPv6. CME MDP3 is IPv4-only — there is no legitimate IPv6 traffic we
        // should be decoding, so drop unconditionally:
        //   * filter active: an IPv6 packet would leave dst_ip = 0 and could
        //     match a wildcard-port allowlist entry, feeding garbage payload
        //     bytes as mdp3_seqnum.
        //   * filter empty: the hardcoded 40-byte IPv6 header would mis-account
        //     the UDP offset because we don't walk extension headers, producing
        //     a non-MDP3 packet downstream and corrupting seqnum tracking just
        //     as silently.
        // A stray IPv6 frame is almost always host-network leakage rather than
        // MDP3 — count it as a filter-drop so EOF stats surface it.
        filtered_drop_count++;
        return false;
      } else {
        ERRF(boost::format("Unsupported ethertype: 0x%04x") % ethertype);
      }

      offset += ip_header_len;

      // UDP header is 8 bytes
      if (caplen < offset + 8) {
        ERRF(boost::format("Packet too small for UDP header: %u bytes (offset=%u)") % caplen % offset);
      }

      uint16_t dst_port;
      memcpy(&dst_port, packet + offset + 2, 2);
      dst_port = ntohs(dst_port);

      offset += 8; // Skip UDP header

      // Consolidated-layout filter: drop packets not in the allowlist. A single
      // 10-minute consolidated PCAP carries every CME GLBX channel muxed
      // together, so without this the decoder would see cross-channel packets
      // and seqnum tracking would scramble immediately.
      if (!pkt_filter.empty()) {
        bool match = false;
        for (const auto& f : pkt_filter) {
          if (f.dst_port != dst_port) continue;
          if (f.dst_ip == 0 || f.dst_ip == dst_ip) { match = true; break; }
        }
        if (!match) {
          filtered_drop_count++;
          return false;
        }
      }

      // Any hardware-timestamp trailer (e.g. Metamako) is appended to the L2
      // frame *after* the UDP payload, so it must be excluded from the MDP3
      // payload we hand to the decoder.
      const uint32_t trailer_size = trailer_spec.present() ? trailer_spec.size : 0;
      if (caplen < offset + trailer_size) {
        ERRF(boost::format("Packet too small for UDP payload + trailer: %u bytes (offset=%u, trailer=%u)")
             % caplen % offset % trailer_size);
      }

      // UDP payload (trailer excluded)
      const u_char *udp_payload = packet + offset;
      uint32_t payload_len = caplen - offset - trailer_size;

      if (payload_len > mcast_recv::msgsz) {
        ERRF(boost::format("Payload too large: %u bytes (max %zu)") % payload_len % mcast_recv::msgsz);
      }

      // Extract the hardware capture timestamp from the trailer, if configured.
      uint64_t hw_ts = 0;
      if (trailer_spec.present()) {
        const u_char *trailer = packet + caplen - trailer_size;
        hw_ts = trailer_timestamp_ns(trailer, trailer_spec);

        // On the first packet, verify the configured trailer actually matches
        // the wire, so a mis-configuration fails loudly instead of silently
        // corrupting payloads / timestamps.
        if (!trailer_validated) {
          const uint32_t frame_wo_trailer = ip_start + ip_total_len;
          const uint64_t pcap_secs = timestamp_ns / 1000000000ULL;
          if (!trailer_looks_valid(caplen, frame_wo_trailer, hw_ts / 1000000000ULL, pcap_secs, trailer_spec)) {
            ERRF(boost::format("Trailer config does not match wire in '%s': caplen=%u, eth+ip_total=%u+%u, "
                               "trailer_secs=%lu, pcap_secs=%lu")
                 % pcap_file % caplen % ip_start % ip_total_len % (hw_ts / 1000000000ULL) % pcap_secs);
          }
          trailer_validated = true;
        }
      }

      // MDP3 UDP payload: first 4 bytes are MsgSeqNum (uint32, little-endian)
      uint32_t mdp3_seqnum;
      memcpy(&mdp3_seqnum, udp_payload, 4);

      // Create ProcessQ message to send to MsgBuf/MessageProcessor
      auto msg = new msg::ProcessQ<seqnumT>();
      memcpy(&msg->buf.message[0], udp_payload, payload_len);
      msg->buf.len = payload_len;
      msg->buf.seqnum = mdp3_seqnum;
      msg->buf.src_ip = src_ip;
      msg->buf.dst_port = dst_port;
      msg->buf.chan = 'A';  // Databento data is de-duplicated, always use Feed A
      msg->buf.recv_ts = timestamp_ns;  // software (pcap record header) capture ts
      msg->buf.hw_ts = hw_ts;           // hardware (trailer) capture ts, 0 if no trailer configured
      if (packet_count < 3)
          std::cerr << "[PCAPReader] file=" << pcap_file << " pkt#" << packet_count << " mdp3_seqnum=" << mdp3_seqnum << "\n";
      last_mdp3_seqnum = mdp3_seqnum;
      packet_count++;

      // Send to message processor (MsgBuf)
      msg_processor->send(msg, this);
      return true;
    }

  public:
    PCAPReader(
        const std::string &chan_nam,
        actors::Actor *msg_processor,
        const std::string &pcap_filename,
        bool big_endian = false,
        const TrailerSpec &trailer_spec = TrailerSpec{})
        : chan_nam(chan_nam),
          msg_processor(msg_processor),
          pcap_file(pcap_filename),
          big_endian(big_endian),
          trailer_spec(trailer_spec)
    {
      snprintf(name, sizeof(name), "%sPCAPReader", chan_nam.c_str());

      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::ShutdownThisActor, shutdown_this_actor_handler);
    }

    // Filtered ctor for the Databento "consolidated" layout: each pcap file
    // carries every channel muxed together, so we need a per-packet
    // (dst_ip, dst_port) allowlist. dst_ip values are network-byte-order
    // uint32_t (e.g. from inet_pton).
    //
    // DO NOT pass dst_ip == 0 ("any IP for this port"). The match loop still
    // honors that sentinel, but the wildcard short-circuits the strict IR-only
    // filter whenever incr_port == port_ir (true for every CME channel today:
    // port_ir = 14000+chan = port_a), letting MBP-snapshot packets leak into
    // the IR decode path and corrupt instrument definitions. The only
    // sanctioned caller — dbento_pcap_parse/src/dbento_pcap_to_bin.hpp —
    // explicitly rejects dst_ip==0 (and the "0.0.0.0" textual address that
    // inet_pton resolves to 0). If you need port-only matching for a new
    // layout, fix the channel's mdp3_prod.info entry to carry the real
    // multicast IP instead of re-introducing the wildcard here.
    PCAPReader(
        const std::string &chan_nam,
        actors::Actor *msg_processor,
        const std::string &pcap_filename,
        const std::vector<std::pair<uint32_t, uint16_t>> &filter,
        bool big_endian = false,
        const TrailerSpec &trailer_spec = TrailerSpec{})
        : chan_nam(chan_nam),
          msg_processor(msg_processor),
          pcap_file(pcap_filename),
          big_endian(big_endian),
          trailer_spec(trailer_spec)
    {
      snprintf(name, sizeof(name), "%sPCAPReader", chan_nam.c_str());

      pkt_filter.reserve(filter.size());
      for (const auto& [ip, port] : filter) pkt_filter.push_back({ip, port});

      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::ShutdownThisActor, shutdown_this_actor_handler);
    }

    ~PCAPReader()
    {
      if (pcap) {
        pcap_close(pcap);
        // NOTE: pcap_close() also closes the underlying FILE*, so don't pclose() again
      }
    }

  private:
    void start_handler(const actors::msg::Start *) noexcept
    {
      open_pcap();
      send(new actors::msg::Continue(), 0);
      log_inf("PCAPReader started from file: %s", pcap_file.c_str());
    }

    void continue_handler(const actors::msg::Continue *) noexcept
    {
      // Drain filter-dropped packets inline. In consolidated mode a single
      // channel's filter rejects ~95% of packets; posting a heap-allocated
      // Continue per drop multiplies allocator + scheduler traffic for no
      // benefit. Loop until we either forward an accepted packet, hit the
      // yield cap, or hit EOF/error — then post exactly one Continue (or
      // ShutdownThisActor on EOF).
      constexpr int kMaxPerTick = 1024;
      pcap_pkthdr *header;
      const u_char *packet;

      for (int n = 0; n < kMaxPerTick; ++n) {
        int ret = pcap_next_ex(pcap, &header, &packet);

        if (ret == 1) {
          // Convert pcap timestamp to nanoseconds
          uint64_t timestamp_ns = header->ts.tv_sec * 1000000000ULL + header->ts.tv_usec * 1000ULL;
          bool accepted = process_packet(packet, header->caplen, header->len, timestamp_ns);
          if (accepted) {
            send(new actors::msg::Continue(), 0);
            return;
          }
          // Filter-drop: read the next packet inline without rescheduling.
          continue;
        }

        if (ret == -2) {
          // End of file
          log_inf("PCAP playback complete: %lu packets processed, %lu filtered out",
                  packet_count, filtered_drop_count);
          std::cerr << "[PCAPReader] EOF file=" << pcap_file
                    << " total_packets=" << packet_count
                    << " filtered_dropped=" << filtered_drop_count
                    << " last_seqnum=" << last_mdp3_seqnum << "\n";
          // If a filter was active and matched ZERO packets, the run is almost
          // certainly misconfigured (wrong channel, stale config, CME mcast-IP
          // migration). Per-channel mode without a filter can legitimately have
          // zero packets, so only warn when a filter was active.
          if (!pkt_filter.empty() && packet_count == 0) {
            std::cerr << "[PCAPReader] WARNING: file=" << pcap_file
                      << " produced ZERO matching packets with an active filter ("
                      << filtered_drop_count << " dropped). Likely cause: wrong "
                         "port/IP for this channel, stale genconfig/mdp3_prod.info, "
                         "or CME multicast-IP migration.\n";
          }
          // Send ShutdownThisActor to self to signal completion; the Group will
          // remove this actor and notify the requester.
          send(new actors::msg::ShutdownThisActor(), 0);
          return;
        }

        if (ret == 0) {
          // Timeout (shouldn't happen with offline files)
          ERR("PCAP timeout - got a fucking error (unexpected for offline file)");
          return;
        }

        // ret == -1: error
        ERRF(boost::format("PCAP error - got a fucking error: %s") % pcap_geterr(pcap));
        return;
      }

      // Yield cap reached with nothing accepted — reschedule so other actors run.
      send(new actors::msg::Continue(), 0);
    }

    void shutdown_handler(const actors::msg::Shutdown *) noexcept
    {
      if (pcap) {
        pcap_close(pcap);
        pcap = nullptr;
        // NOTE: pcap_close() also closes the underlying FILE*, so don't pclose() again
      }
      log_inf("PCAPReader shutdown (full group shutdown)");
    }

    void shutdown_this_actor_handler(const actors::msg::ShutdownThisActor *) noexcept
    {
      if (pcap) {
        pcap_close(pcap);
        pcap = nullptr;
        // NOTE: pcap_close() also closes the underlying FILE*, so don't pclose() again
      }
      log_inf("PCAPReader shutdown (individual actor complete)");
    }
  };

}
