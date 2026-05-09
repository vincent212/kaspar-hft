#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include <limits>
#include <arpa/inet.h>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/ShutdownThisActor.hpp"
#include "actors/msg/Continue.hpp"
#include "mcast_recv/message_buffer.hpp"
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

    pcap_t *pcap = nullptr;
    uint64_t packet_count = 0;
    uint32_t last_mdp3_seqnum = 0;

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
        // Decompress with zstdcat
        std::string cmd = "zstdcat '" + pcap_file + "'";
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

    void process_packet(const u_char *packet, uint32_t caplen, [[maybe_unused]] uint32_t orig_len, [[maybe_unused]] uint64_t timestamp_ns)
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
      uint32_t src_ip = 0;
      uint32_t ip_start = offset;

      if (ethertype == 0x0800) {
        // IPv4
        if (caplen < offset + 20) {
          ERRF(boost::format("Packet too small for IPv4 header: %u bytes") % caplen);
        }
        uint8_t ihl = (packet[offset] & 0x0F);
        ip_header_len = ihl * 4;
        memcpy(&src_ip, packet + ip_start + 12, 4);  // src IP at byte 12 of IPv4 header, network byte order

      } else if (ethertype == 0x86DD) {
        // IPv6
        ip_header_len = 40;
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

      // UDP payload
      const u_char *udp_payload = packet + offset;
      uint32_t payload_len = caplen - offset;

      if (payload_len > mcast_recv::msgsz) {
        ERRF(boost::format("Payload too large: %u bytes (max %zu)") % payload_len % mcast_recv::msgsz);
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
      msg->buf.recv_ts = timestamp_ns;  // Use PCAP capture timestamp
      if (packet_count < 3)
          std::cerr << "[PCAPReader] file=" << pcap_file << " pkt#" << packet_count << " mdp3_seqnum=" << mdp3_seqnum << "\n";
      last_mdp3_seqnum = mdp3_seqnum;
      packet_count++;

      // Send to message processor (MsgBuf)
      msg_processor->send(msg, this);
    }

  public:
    PCAPReader(
        const std::string &chan_nam,
        actors::Actor *msg_processor,
        const std::string &pcap_filename,
        bool big_endian = false)
        : chan_nam(chan_nam),
          msg_processor(msg_processor),
          pcap_file(pcap_filename),
          big_endian(big_endian)
    {
      snprintf(name, sizeof(name), "%sPCAPReader", chan_nam.c_str());

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
      // Read ONE packet per Continue message (like BFA reads one record)
      pcap_pkthdr *header;
      const u_char *packet;
      int ret = pcap_next_ex(pcap, &header, &packet);

      switch (ret)
      {
      case 1:
        // Legitimate packet
        {
          // Convert pcap timestamp to nanoseconds
          uint64_t timestamp_ns = header->ts.tv_sec * 1000000000ULL + header->ts.tv_usec * 1000ULL;
          process_packet(packet, header->caplen, header->len, timestamp_ns);
          // Schedule next packet
          send(new actors::msg::Continue(), 0);
        }
        break;

      case 0:
        // Timeout (shouldn't happen with offline files)
        ERR("PCAP timeout - got a fucking error (unexpected for offline file)");
        break;

      case -1:
        // Error
        ERRF(boost::format("PCAP error - got a fucking error: %s") % pcap_geterr(pcap));
        break;

      case -2:
        // End of file
        log_inf("PCAP playback complete: %lu packets processed", packet_count);
        std::cerr << "[PCAPReader] EOF file=" << pcap_file << " total_packets=" << packet_count << " last_seqnum=" << last_mdp3_seqnum << "\n";
        // Send ShutdownThisActor to self to signal completion
        // Group will remove this actor and notify the requester
        send(new actors::msg::ShutdownThisActor(), 0);
        break;
      }
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
