/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_md/Starbase.hpp"

#include <bit>
#include <cstring>
#include <sstream>

#include "cb_deribit_gateway_sbe_marketdata_api/AskDelete.h"
#include "cb_deribit_gateway_sbe_marketdata_api/AskPut.h"
#include "cb_deribit_gateway_sbe_marketdata_api/AskQtyReduced.h"
#include "cb_deribit_gateway_sbe_marketdata_api/BidDelete.h"
#include "cb_deribit_gateway_sbe_marketdata_api/BidPut.h"
#include "cb_deribit_gateway_sbe_marketdata_api/BidQtyReduced.h"
#include "cb_deribit_gateway_sbe_marketdata_api/BlockTrade.h"
#include "cb_deribit_gateway_sbe_marketdata_api/EndOfCycle.h"
#include "cb_deribit_gateway_sbe_marketdata_api/IndexDefinition.h"
#include "cb_deribit_gateway_sbe_marketdata_api/IndexInfo.h"
#include "cb_deribit_gateway_sbe_marketdata_api/InstrumentDefinition.h"
#include "cb_deribit_gateway_sbe_marketdata_api/InstrumentInfo.h"
#include "cb_deribit_gateway_sbe_marketdata_api/InstrumentRef.h"
#include "cb_deribit_gateway_sbe_marketdata_api/InstrumentStatusUpdate.h"
#include "cb_deribit_gateway_sbe_marketdata_api/MdMessageHeader.h"
#include "cb_deribit_gateway_sbe_marketdata_api/RetransmitReject.h"
#include "cb_deribit_gateway_sbe_marketdata_api/RetransmitRequest.h"
#include "cb_deribit_gateway_sbe_marketdata_api/SnapshotHeader.h"
#include "cb_deribit_gateway_sbe_marketdata_api/SnapshotTrailer.h"
#include "cb_deribit_gateway_sbe_marketdata_api/Trade.h"
#include "cb_deribit_gateway_sbe_marketdata_api/TradeSummary.h"

namespace deribit::starbase
{
  namespace g = cb::deribit::gateway::sbe::marketdata::api;

  static_assert(std::endian::native == std::endian::little,
                "PacketHeader is read with memcpy: the wire is little-endian");
  static_assert(g::MdMessageHeader::encodedLength() == kMessageHeaderSize);

  const char *to_string(Status s)
  {
    switch (s)
    {
    case Status::Ok: return "ok";
    case Status::Short: return "short";
    case Status::Truncated: return "truncated";
    case Status::BadLength: return "bad length";
    }
    return "?";
  }

  namespace
  {
    template <class T>
    T rd(const uint8_t *p)
    {
      T v;
      std::memcpy(&v, p, sizeof v);
      return v;
    }
    template <class T>
    void wr(uint8_t *p, T v)
    {
      std::memcpy(p, &v, sizeof v);
    }
    char *cp(const uint8_t *p) { return const_cast<char *>(reinterpret_cast<const char *>(p)); }

    // Wrap a generated flyweight on a body of `len` bytes; false if the body is
    // shorter than the template's block.
    template <class M>
    bool wrap(M &m, const uint8_t *body, size_t len, uint16_t version)
    {
      if (len < M::sbeBlockLength())
        return false;
      m.wrapForDecode(cp(body), 0, M::sbeBlockLength(), version, len);
      return true;
    }

    std::string trim(std::string s)
    {
      auto n = s.find('\0');
      if (n != std::string::npos)
        s.resize(n);
      return s;
    }
  }

  bool decode_body(uint16_t tid, uint16_t version, const uint8_t *body, size_t len, Body &out)
  {
    switch (tid)
    {
    case 20:
    case 21:
    {
      // AskPut has the same layout as BidPut (schema).
      g::BidPut m;
      if (!wrap(m, body, len, version))
        return false;
      out = Put{tid == 20, m.orderId(), m.instrumentId(), m.quantityMantissa(), m.price().price9(),
                m.sortOrderId()};
      return true;
    }
    case 22:
    case 23:
    {
      g::BidQtyReduced m;
      if (!wrap(m, body, len, version))
        return false;
      out = QtyReduced{tid == 22, m.orderId(), m.instrumentId(), m.quantityMantissa()};
      return true;
    }
    case 24:
    case 25:
    {
      g::BidDelete m;
      if (!wrap(m, body, len, version))
        return false;
      out = Delete{tid == 24, m.orderId(), m.instrumentId()};
      return true;
    }
    case 30:
    {
      g::TradeSummary m;
      if (!wrap(m, body, len, version))
        return false;
      TradeSummary t;
      t.instrumentId = m.instrumentId();
      t.takerOrderId = m.takerOrderId();
      t.totalFilled = m.totalFilledMantissa();
      t.deepestPrice9 = m.deepestPrice().price9();
      t.markPrice9 = m.markPrice().price9();
      t.indexPrice9 = m.indexPrice().price9();
      t.tradeCount = m.tradeCount();
      t.takerFlags = m.takerFlags().rawValue();
      out = t;
      return true;
    }
    case 31:
    {
      g::Trade m;
      if (!wrap(m, body, len, version))
        return false;
      out = Trade{m.matchId(), m.instrumentId(), m.makerOrderId(), m.fillQtyMantissa(),
                  m.fillPrice().price9(), m.makerFlags().rawValue()};
      return true;
    }
    case 33:
    {
      g::BlockTrade m;
      if (!wrap(m, body, len, version))
        return false;
      out = BlockTrade{m.matchId(), m.instrumentId(), m.blockTradeId(), m.fillQtyMantissa(),
                       m.fillPrice().price9(), m.takerFlags().rawValue()};
      return true;
    }
    case 10:
    {
      g::InstrumentDefinition m;
      if (!wrap(m, body, len, version))
        return false;
      InstrumentDefinition d;
      d.instrumentId = m.instrumentId();
      d.name = trim(m.getNameAsString());
      d.indexId = m.indexId();
      d.underlying = trim(m.getUnderlyingAsString());
      d.quantityAsset = trim(m.getQuantityAssetAsString());
      d.priceAsset = trim(m.getPriceAssetAsString());
      d.expiryTime = m.expiryTime();
      d.strikePrice9 = m.strikePrice().price9();
      d.minOrderQuantity = m.minOrderQuantity();
      d.tickSize9 = m.tickSize().price9();
      d.quantityExponent = m.quantityExponent();
      d.type = m.typeRaw();
      d.flags = m.flags().rawValue();
      d.status = m.statusRaw();
      try
      {
        auto &lt = m.largeTickSizes();
        while (lt.hasNext())
        {
          lt.next();
          d.largeTickSizes.emplace_back(lt.largeTickSize().price9(), lt.thresholdPrice().price9());
        }
        auto &legs = m.legs();
        while (legs.hasNext())
        {
          legs.next();
          d.legs.emplace_back(legs.legInstrumentId(), legs.ratio());
        }
      }
      catch (const std::exception &)
      {
        return false;   // a group past the end of the body
      }
      out = std::move(d);
      return true;
    }
    case 16:
    {
      g::InstrumentStatusUpdate m;
      if (!wrap(m, body, len, version))
        return false;
      out = InstrumentStatusUpdate{m.instrumentId(), m.tradingStatusRaw()};
      return true;
    }
    case 100:
    {
      g::SnapshotHeader m;
      if (!wrap(m, body, len, version))
        return false;
      out = SnapshotBound{true, m.instrumentId(), m.incrementalTimestamp(), m.incrementalSeqNum()};
      return true;
    }
    case 101:
    {
      g::SnapshotTrailer m;
      if (!wrap(m, body, len, version))
        return false;
      out = SnapshotBound{false, m.instrumentId(), m.timestamp(), m.incrementSeqNum()};
      return true;
    }
    case 119:
    {
      g::EndOfCycle m;
      if (!wrap(m, body, len, version))
        return false;
      out = EndOfCycle{m.activeInstrumentCount()};
      return true;
    }
    case 11: case 12: case 14: case 15: case 200: case 202:
      out = Other{};
      return true;
    default:
      out = Unknown{};
      return true;
    }
  }

  Status decode_packet(const uint8_t *buf, size_t len, PacketHeader &ph, std::vector<Message> &out)
  {
    if (len < kPacketHeaderSize)
      return Status::Short;
    ph.sendingTime = rd<int64_t>(buf);
    ph.seqNum = rd<int64_t>(buf + 8);
    ph.channelId = rd<int32_t>(buf + 16);
    ph.type = rd<uint16_t>(buf + 20);
    ph.messageCount = rd<uint16_t>(buf + 22);
    size_t off = kPacketHeaderSize;
    for (uint16_t i = 0; i < ph.messageCount; ++i)
    {
      if (off + kMessageHeaderSize > len)
        return Status::Truncated;
      g::MdMessageHeader h(cp(buf), off, len, 2);
      Message m;
      m.seq = ph.seqNum + i;
      m.length = h.messageLength();
      m.templateId = h.templateId();
      m.version = h.version();
      m.flags = h.flags().rawValue();
      m.transactTime = h.transactTime();
      if (m.length < kMessageHeaderSize)
        return Status::BadLength;
      if (off + m.length > len)
        return Status::Truncated;
      if (!decode_body(m.templateId, m.version, buf + off + kMessageHeaderSize,
                       m.length - kMessageHeaderSize, m.body))
        return Status::BadLength;
      out.push_back(std::move(m));
      off += m.length;
    }
    return Status::Ok;
  }

  // ---- dump ---------------------------------------------------------------------------

  std::string dump_packet_header(const PacketHeader &ph)
  {
    std::ostringstream o;
    o << "P " << ph.sendingTime << " " << ph.seqNum << " " << ph.channelId << " " << ph.type << " "
      << ph.messageCount;
    return o.str();
  }

  std::string dump(const uint8_t *msg, size_t len, int64_t seq)
  {
    std::ostringstream o;
    g::MdMessageHeader h(cp(msg), 0, len, 2);
    const uint16_t tid = h.templateId(), ver = h.version();
    o << "M " << seq << " " << h.messageLength() << " " << tid << " " << ver << " "
      << h.flags().rawValue() << " " << h.transactTime();
    const uint8_t *b = msg + kMessageHeaderSize;
    const size_t bl = h.messageLength() - kMessageHeaderSize;
    auto dbl = [](double d) { return std::bit_cast<int64_t>(d); };
    auto str = [](std::string s) { return "\"" + trim(std::move(s)) + "\""; };
    switch (tid)
    {
    case 20: case 21:
    {
      g::BidPut m;
      if (wrap(m, b, bl, ver))
        o << " " << m.orderId() << " " << m.instrumentId() << " " << m.quantityMantissa() << " "
          << m.price().price9() << " " << m.sortOrderId();
      break;
    }
    case 22: case 23:
    {
      g::BidQtyReduced m;
      if (wrap(m, b, bl, ver))
        o << " " << m.orderId() << " " << m.instrumentId() << " " << m.quantityMantissa();
      break;
    }
    case 24: case 25:
    {
      g::BidDelete m;
      if (wrap(m, b, bl, ver))
        o << " " << m.orderId() << " " << m.instrumentId();
      break;
    }
    case 30:
    {
      g::TradeSummary m;
      if (wrap(m, b, bl, ver))
        o << " " << m.instrumentId() << " " << m.takerOrderId() << " " << m.totalFilledMantissa()
          << " " << m.deepestPrice().price9() << " " << m.markPrice().price9() << " "
          << m.indexPrice().price9() << " " << m.tradeCount() << " " << m.takerFlags().rawValue();
      break;
    }
    case 31:
    {
      g::Trade m;
      if (wrap(m, b, bl, ver))
        o << " " << m.matchId() << " " << m.instrumentId() << " " << m.makerOrderId() << " "
          << m.fillQtyMantissa() << " " << m.fillPrice().price9() << " " << m.makerFlags().rawValue();
      break;
    }
    case 33:
    {
      g::BlockTrade m;
      if (wrap(m, b, bl, ver))
        o << " " << m.matchId() << " " << m.instrumentId() << " " << m.blockTradeId() << " "
          << m.blockRfqId() << " " << m.fillQtyMantissa() << " " << m.fillPrice().price9() << " "
          << m.markPrice().price9() << " " << m.indexPrice().price9() << " "
          << dbl(m.impliedVolatility()) << " " << m.takerFlags().rawValue() << " "
          << m.numberOfLegs() << " " << m.executionTimestamp();
      break;
    }
    case 10:
    {
      g::InstrumentDefinition m;
      if (wrap(m, b, bl, ver))
      {
        o << " " << m.instrumentId() << " " << str(m.getNameAsString()) << " " << m.indexId() << " "
          << str(m.getUnderlyingAsString()) << " " << str(m.getQuantityAssetAsString()) << " "
          << str(m.getPriceAssetAsString()) << " " << m.expiryTime() << " "
          << m.strikePrice().price9() << " " << m.minOrderQuantity() << " "
          << m.tickSize().price9() << " " << int(m.quantityExponent()) << " " << int(m.typeRaw())
          << " " << int(m.flags().rawValue()) << " " << int(m.statusRaw()) << " [";
        auto &lt = m.largeTickSizes();
        bool first = true;
        while (lt.hasNext())
        {
          lt.next();
          o << (first ? "" : ";") << lt.largeTickSize().price9() << "," << lt.thresholdPrice().price9();
          first = false;
        }
        o << "] [";
        auto &legs = m.legs();
        first = true;
        while (legs.hasNext())
        {
          legs.next();
          o << (first ? "" : ";") << legs.legInstrumentId() << "," << int(legs.ratio());
          first = false;
        }
        o << "]";
      }
      break;
    }
    case 11:
    {
      g::IndexDefinition m;
      if (wrap(m, b, bl, ver))
        o << " " << m.indexId() << " " << str(m.getNameAsString());
      break;
    }
    case 12:
    {
      g::IndexInfo m;
      if (wrap(m, b, bl, ver))
        o << " " << m.indexId() << " " << m.indexPrice().price9();
      break;
    }
    case 14:
    {
      g::InstrumentInfo m;
      if (wrap(m, b, bl, ver))
        o << " " << m.instrumentId() << " " << m.minSellPrice().price9() << " "
          << m.maxBuyPrice().price9() << " " << m.markPrice().price9();
      break;
    }
    case 15:
    {
      g::InstrumentRef m;
      if (wrap(m, b, bl, ver))
        o << " " << m.instrumentId() << " " << dbl(m.currentFunding()) << " " << dbl(m.funding8h())
          << " " << m.estimatedDeliveryPrice().price9() << " " << m.deliveryPrice().price9() << " "
          << m.settlementPrice().price9() << " " << dbl(m.openInterest());
      break;
    }
    case 16:
    {
      g::InstrumentStatusUpdate m;
      if (wrap(m, b, bl, ver))
        o << " " << m.instrumentId() << " " << int(m.tradingStatusRaw());
      break;
    }
    case 100:
    {
      g::SnapshotHeader m;
      if (wrap(m, b, bl, ver))
        o << " " << m.instrumentId() << " " << m.incrementalTimestamp() << " " << m.incrementalSeqNum();
      break;
    }
    case 101:
    {
      g::SnapshotTrailer m;
      if (wrap(m, b, bl, ver))
        o << " " << m.instrumentId() << " " << m.timestamp() << " " << m.incrementSeqNum();
      break;
    }
    case 119:
    {
      g::EndOfCycle m;
      if (wrap(m, b, bl, ver))
        o << " " << m.activeInstrumentCount();
      break;
    }
    default:
      break;
    }
    return o.str();
  }

  // ---- encode (tests) -----------------------------------------------------------------

  namespace
  {
    template <class M>
    size_t enc_put(uint8_t *b, size_t cap, const Put &p)
    {
      M m;
      m.wrapForEncode(cp(b), 0, cap);
      m.orderId(p.orderId).instrumentId(p.instrumentId).quantityMantissa(p.qty);
      m.price().price9(p.price9);
      m.sortOrderId(p.sortOrderId);
      return m.encodedLength();
    }
    template <class M>
    size_t enc_red(uint8_t *b, size_t cap, const QtyReduced &p)
    {
      M m;
      m.wrapForEncode(cp(b), 0, cap);
      m.orderId(p.orderId).instrumentId(p.instrumentId).quantityMantissa(p.qty);
      return m.encodedLength();
    }
    template <class M>
    size_t enc_del(uint8_t *b, size_t cap, const Delete &p)
    {
      M m;
      m.wrapForEncode(cp(b), 0, cap);
      m.orderId(p.orderId).instrumentId(p.instrumentId);
      return m.encodedLength();
    }

    size_t encode_body(uint8_t *b, size_t cap, const Body &body, uint16_t &tid)
    {
      if (auto p = std::get_if<Put>(&body))
      {
        tid = p->bid ? 20 : 21;
        return p->bid ? enc_put<g::BidPut>(b, cap, *p) : enc_put<g::AskPut>(b, cap, *p);
      }
      if (auto p = std::get_if<QtyReduced>(&body))
      {
        tid = p->bid ? 22 : 23;
        return p->bid ? enc_red<g::BidQtyReduced>(b, cap, *p) : enc_red<g::AskQtyReduced>(b, cap, *p);
      }
      if (auto p = std::get_if<Delete>(&body))
      {
        tid = p->bid ? 24 : 25;
        return p->bid ? enc_del<g::BidDelete>(b, cap, *p) : enc_del<g::AskDelete>(b, cap, *p);
      }
      if (auto p = std::get_if<TradeSummary>(&body))
      {
        tid = 30;
        g::TradeSummary m;
        m.wrapForEncode(cp(b), 0, cap);
        m.instrumentId(p->instrumentId).takerOrderId(p->takerOrderId).totalFilledMantissa(p->totalFilled);
        m.deepestPrice().price9(p->deepestPrice9);
        m.markPrice().price9(p->markPrice9);
        m.indexPrice().price9(p->indexPrice9);
        m.tradeCount(p->tradeCount);
        m.takerFlags().rawValue(p->takerFlags);
        return m.encodedLength();
      }
      if (auto p = std::get_if<Trade>(&body))
      {
        tid = 31;
        g::Trade m;
        m.wrapForEncode(cp(b), 0, cap);
        m.matchId(p->matchId).instrumentId(p->instrumentId).makerOrderId(p->makerOrderId)
            .fillQtyMantissa(p->qty);
        m.fillPrice().price9(p->price9);
        m.makerFlags().rawValue(p->makerFlags);
        return m.encodedLength();
      }
      if (auto p = std::get_if<InstrumentStatusUpdate>(&body))
      {
        tid = 16;
        g::InstrumentStatusUpdate m;
        m.wrapForEncode(cp(b), 0, cap);
        m.instrumentId(p->instrumentId).tradingStatus(g::InstrumentStatus::get(p->tradingStatus));
        return m.encodedLength();
      }
      if (auto p = std::get_if<SnapshotBound>(&body))
      {
        tid = p->header ? 100 : 101;
        if (p->header)
        {
          g::SnapshotHeader m;
          m.wrapForEncode(cp(b), 0, cap);
          m.instrumentId(p->instrumentId).incrementalTimestamp(p->incrementalTimestamp)
              .incrementalSeqNum(p->incrementalSeqNum);
          return m.encodedLength();
        }
        g::SnapshotTrailer m;
        m.wrapForEncode(cp(b), 0, cap);
        m.instrumentId(p->instrumentId).timestamp(p->incrementalTimestamp).incrementSeqNum(p->incrementalSeqNum);
        return m.encodedLength();
      }
      if (auto p = std::get_if<EndOfCycle>(&body))
      {
        tid = 119;
        g::EndOfCycle m;
        m.wrapForEncode(cp(b), 0, cap);
        m.activeInstrumentCount(p->activeInstrumentCount);
        return m.encodedLength();
      }
      if (auto p = std::get_if<InstrumentDefinition>(&body))
      {
        tid = 10;
        g::InstrumentDefinition m;
        m.wrapForEncode(cp(b), 0, cap);
        m.instrumentId(p->instrumentId).putName(p->name).indexId(p->indexId).putUnderlying(p->underlying)
            .putQuantityAsset(p->quantityAsset).putPriceAsset(p->priceAsset).expiryTime(p->expiryTime);
        m.strikePrice().price9(p->strikePrice9);
        m.minOrderQuantity(p->minOrderQuantity);
        m.tickSize().price9(p->tickSize9);
        m.quantityExponent(p->quantityExponent).type(g::InstrumentType::get(p->type));
        m.flags().rawValue(p->flags);
        m.status(g::InstrumentStatus::get(p->status));
        auto &lt = m.largeTickSizesCount(uint16_t(p->largeTickSizes.size()));
        for (const auto &[t, th] : p->largeTickSizes)
        {
          lt.next();
          lt.largeTickSize().price9(t);
          lt.thresholdPrice().price9(th);
        }
        auto &legs = m.legsCount(uint16_t(p->legs.size()));
        for (const auto &[i, r] : p->legs)
          legs.next().legInstrumentId(i).ratio(r);
        return m.encodedLength();
      }
      return 0;
    }
  }

  std::vector<uint8_t> encode_packet(const PacketHeader &ph, const std::vector<Message> &msgs)
  {
    std::vector<uint8_t> out(kPacketHeaderSize, 0);
    wr(out.data(), ph.sendingTime);
    wr(out.data() + 8, ph.seqNum);
    wr(out.data() + 16, ph.channelId);
    wr(out.data() + 20, ph.type);
    wr(out.data() + 22, uint16_t(msgs.size()));
    for (const auto &m : msgs)
    {
      uint8_t body[1024] = {};
      uint16_t tid = 0;
      const size_t bl = encode_body(body, sizeof body, m.body, tid);
      const size_t len = (kMessageHeaderSize + bl + 7) / 8 * 8;   // padded to 8 (feed.md 3)
      const size_t at = out.size();
      out.resize(at + len, 0);
      g::MdMessageHeader h(cp(out.data()), at, out.size(), 2);
      h.messageLength(uint16_t(len)).templateId(tid).version(m.version);
      h.flags().rawValue(m.flags);
      h.transactTime(m.transactTime);
      std::memcpy(out.data() + at + kMessageHeaderSize, body, bl);
    }
    return out;
  }
}
