#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The MDP3 decoder on the card: one packet's bytes in, events out
 * (events.hpp). The field decoders are generated from CME's template
 * (gen_decoder.py -> generated/mdp3_messages.hpp); this file is the packet
 * walk, the same as DataDecoder::mbo_data and decode_one on the CPU:
 *
 *   packet  = MsgSeqNum (4) | SendingTime (8) | message ...
 *   message = MsgSize (2, counts itself) | SBE header (8: block length,
 *             template id, schema id, version) | root block | groups ...
 *
 * Root blocks and group entries are stepped by the lengths on the wire, not
 * the schema's, so a newer CME version with longer blocks still decodes.
 * Templates other than 46, 48, 30 and 4 are skipped by their MsgSize. A
 * corrupt length ends the walk of that packet; the EndOfPacket still goes out.
 *
 * Phase 1 of MDP3_PLAN.md: store, then walk. Written for correctness first;
 * the latency work comes once it matches the reference.
 */

#include <ap_axi_sdata.h>
#include <hls_stream.h>

#include "generated/mdp3_messages.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

// The UDP payload, 8 bytes a word, little-endian, as the network side
// delivers it: `keep` marks the valid bytes, `last` the packet's last word.
using InWord = ap_axiu<64, 0, 0, 0>;

constexpr int kMaxPacket = 1536;   // bytes; a CME UDP payload is at most about 1.4 KB
constexpr int kSbeHeader = 10;     // MsgSize + the 8-byte SBE message header

namespace detail {

inline uint16_t u16(const uint8_t *b, int at) { return mdp3gen::le<uint16_t, 2>(b, at); }

template <class M>
inline void send(hls::stream<kfpga::Envelope> &out, const M &m, uint32_t &events)
{
#pragma HLS INLINE
  out.write(kfpga::encode(m, kDecoder, kBooks));
  ++events;
}

} // namespace detail

// One packet from `in` to `out`.
inline void decode_packet(hls::stream<InWord> &in, hls::stream<kfpga::Envelope> &out)
{
  using namespace mdp3gen;
  using detail::u16;

  uint8_t b[kMaxPacket];
#pragma HLS ARRAY_PARTITION variable=b type=cyclic factor=8

  // Store the packet.
  int len = 0;
  bool last = false;
store:
  while (!last)
  {
#pragma HLS PIPELINE II=1
    const InWord w = in.read();
    int n = 0;
    for (int i = 0; i < 8; ++i)
    {
#pragma HLS UNROLL
      if (w.keep[i] && len + i < kMaxPacket)
      {
        b[len + i] = w.data.range(8 * i + 7, 8 * i);
        ++n;
      }
    }
    len += n;
    last = w.last;
  }

  EndOfPacket eop;
  eop.bytes = len;
  if (len >= 12)
  {
    eop.seq = le<uint32_t, 4>(b, 0);
    eop.sending_time = le<uint64_t, 8>(b, 4);
  }
  const uint32_t seq = eop.seq;

  // Walk its messages.
  int at = 12;
walk:
  while (at + kSbeHeader <= len)
  {
    const uint16_t msg_size = u16(b, at);
    if (msg_size < kSbeHeader || at + msg_size > len)
      break;   // corrupt
    const uint16_t block = u16(b, at + 2);
    const uint16_t tid = u16(b, at + 4);
    const uint16_t schema = u16(b, at + 6);
    const uint16_t version = u16(b, at + 8);
    const int body = at + kSbeHeader;
    const int end = at + msg_size;
    ++eop.messages;

    if (schema == kSchemaId && tid == MDIncrementalRefreshBook46::kTemplateId)
    {
      using M = MDIncrementalRefreshBook46;
      const M::Root r = M::root(b, body, version);
      int g = body + block;
      const GroupHeader h = M::NoMDEntries_header(b, g);
      g += h.size;
    book:
      for (int i = 0; i < h.count && g + h.block_length <= end; ++i)
      {
        const M::NoMDEntries_Entry e = M::NoMDEntries_entry(b, g, version);
        g += h.block_length;
        BookEntry m;
        m.seq = seq;
        m.transact_time = r.TransactTime;
        m.price = e.MDEntryPx;
        m.size = e.MDEntrySize;
        m.security_id = e.SecurityID;
        m.rpt_seq = e.RptSeq;
        m.orders = e.NumberOfOrders;
        m.flags = e.MDPriceLevel | (uint32_t(e.MDUpdateAction) << 8) | (uint32_t(e.MDEntryType) << 16) |
                  (uint32_t(r.MatchEventIndicator) << 24);
        m.tradeable_size = e.TradeableSize;
        detail::send(out, m, eop.events);
      }
    }
    else if (schema == kSchemaId && tid == MDIncrementalRefreshTradeSummary48::kTemplateId)
    {
      using M = MDIncrementalRefreshTradeSummary48;
      const M::Root r = M::root(b, body, version);
      int g = body + block;
      const GroupHeader h = M::NoMDEntries_header(b, g);
      g += h.size;
    trades:
      for (int i = 0; i < h.count && g + h.block_length <= end; ++i)
      {
        const M::NoMDEntries_Entry e = M::NoMDEntries_entry(b, g, version);
        g += h.block_length;
        Trade m;
        m.seq = seq;
        m.transact_time = r.TransactTime;
        m.price = e.MDEntryPx;
        m.size = e.MDEntrySize;
        m.security_id = e.SecurityID;
        m.rpt_seq = e.RptSeq;
        m.orders = e.NumberOfOrders;
        m.flags = e.AggressorSide | (uint32_t(e.MDUpdateAction) << 8) | (uint32_t(r.MatchEventIndicator) << 24);
        m.trade_id = e.MDTradeEntryID;
        detail::send(out, m, eop.events);
      }
    }
    else if (schema == kSchemaId && tid == SecurityStatus30::kTemplateId)
    {
      const SecurityStatus30::Root r = SecurityStatus30::root(b, body, version);
      SecurityStatus m;
      m.seq = seq;
      m.transact_time = r.TransactTime;
      m.security_id = r.SecurityID;
      m.trade_date = r.TradeDate;
      m.flags = r.SecurityTradingStatus | (uint32_t(r.HaltReason) << 8) | (uint32_t(r.SecurityTradingEvent) << 16) |
                (uint32_t(r.MatchEventIndicator) << 24);
      detail::send(out, m, eop.events);
    }
    else if (schema == kSchemaId && tid == ChannelReset4::kTemplateId)
    {
      using M = ChannelReset4;
      const M::Root r = M::root(b, body, version);
      int g = body + block;
      const GroupHeader h = M::NoMDEntries_header(b, g);
      g += h.size;
    resets:
      for (int i = 0; i < h.count && g + h.block_length <= end; ++i)
      {
        const M::NoMDEntries_Entry e = M::NoMDEntries_entry(b, g, version);
        g += h.block_length;
        ChannelReset m;
        m.seq = seq;
        m.transact_time = r.TransactTime;
        m.appl_id = e.ApplID;
        m.match_event = r.MatchEventIndicator;
        detail::send(out, m, eop.events);
      }
    }
    at = end;
  }

  out.write(kfpga::encode(eop, kDecoder, kBooks));
}

} // namespace mdp3fpga
