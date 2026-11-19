/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/Writer.hpp"

#include "jnx_md/gen/glimpse.hpp"
#include "jnx_md/gen/moldudp64.hpp"

namespace jnx_md
{
  namespace G = jnx::glimpse::from_jnx;
  using Header = jnx::moldudp64::from_jnx::DownstreamPacketHeader;

  std::string mold_packet(std::string_view session, uint64_t seq,
                          const std::vector<std::string> &messages, uint16_t count)
  {
    Header h{};
    std::memset(h.Session, ' ', sizeof(h.Session));
    std::memcpy(h.Session, session.data(), std::min(session.size(), sizeof(h.Session)));
    h.SequenceNumber = seq;
    h.MessageCount = count ? count : uint16_t(messages.size());
    std::string out(reinterpret_cast<const char *>(&h), sizeof(h));
    if (count == 0xFFFF)
      return out;
    for (const auto &m : messages)
    {
      out.push_back(char(m.size() >> 8));
      out.push_back(char(m.size() & 0xFF));
      out += m;
    }
    return out;
  }

  std::vector<std::string> mold_messages(std::string_view p)
  {
    std::vector<std::string> out;
    if (p.size() < sizeof(Header))
      return out;
    Header h;
    std::memcpy(&h, p.data(), sizeof(h));
    size_t off = sizeof(Header);
    const uint16_t count = h.MessageCount;
    if (count == 0xFFFF)
      return out;
    for (uint16_t k = 0; k < count && off + 2 <= p.size(); ++k)
    {
      const size_t n = size_t(uint8_t(p[off])) << 8 | uint8_t(p[off + 1]);
      out.emplace_back(p.substr(off + 2, n));
      off += 2 + n;
    }
    return out;
  }

  std::string soup_packet(char type, std::string_view payload)
  {
    const size_t n = payload.size() + 1;
    std::string out;
    out.push_back(char(n >> 8));
    out.push_back(char(n & 0xFF));
    out.push_back(type);
    out.append(payload);
    return out;
  }

  namespace
  {
    template <class M>
    void put(std::string &out, const M &m)
    {
      out += soup_packet('S', bytes_of(m));
    }

    template <size_t N>
    void copy4(char (&dst)[N], const char *src)
    {
      std::memcpy(dst, src, N);
    }
  }

  std::string glimpse_snapshot(const Handler &h, uint64_t next_seq)
  {
    const Dispatcher &d = h.dispatcher();
    const Instruments &ins = h.instruments();
    const uint32_t ns = d.last_nanos();
    std::string out;

    // Login Accepted: blank session, next sequence 1 (as in the sample).
    {
      std::string la(10, ' ');
      la += std::string(19, ' ') + "1";
      out += soup_packet('A', la);
    }
    {
      G::TimestampSecondsMessage t{};
      t.TimestampSeconds = d.seconds();
      put(out, t);
    }
    {
      G::SystemEventMessage s{};
      s.TimestampNanoseconds = ns;
      s.set_Group("");
      s.SystemEvent = 'O';
      put(out, s);
    }
    for (const auto &[table, rows] : ins.tables())
      for (const auto &[start, tick] : rows)
      {
        G::PriceTickSizeMessage l{};
        l.TimestampNanoseconds = ns;
        l.PriceTickSizeTableId = table;
        l.PriceTickSize = tick;
        l.PriceStart = start;
        put(out, l);
      }
    for (const auto &ip : ins.all())
    {
      const Instrument &i = *ip;
      if (!i.listed)
        continue;
      G::OrderbookDirectoryMessage r{};
      r.TimestampNanoseconds = ns;
      copy4(r.OrderbookId, i.key.data());
      r.set_OrderbookCode(i.isin);
      copy4(r.Group, i.key.data() + 4);
      r.RoundLotSize = i.round_lot;
      r.PriceTickSizeTableId = i.table;
      r.PriceDecimals = i.decimals;
      r.UpperPriceLimit = i.upper;
      r.LowerPriceLimit = i.lower;
      put(out, r);
    }
    for (const auto &ip : ins.all())
    {
      const Instrument &i = *ip;
      if (!i.listed || !i.trading_state)
        continue;
      G::TradingStateMessage t{};
      t.TimestampNanoseconds = ns;
      copy4(t.OrderbookId, i.key.data());
      copy4(t.Group, i.key.data() + 4);
      t.TradingState = i.trading_state;
      put(out, t);
    }
    for (const auto &ip : ins.all())
    {
      const Instrument &i = *ip;
      if (!i.listed || i.short_sell != '1')
        continue;
      G::ShortSellingPriceRestrictionStateMessage y{};
      y.TimestampNanoseconds = ns;
      copy4(y.OrderbookId, i.key.data());
      copy4(y.Group, i.key.data() + 4);
      y.ShortSellingState = '1';
      put(out, y);
    }
    for (const auto &ip : ins.all())
    {
      const Instrument &i = *ip;
      if (!i.listed || !i.ref_price)
        continue;
      G::OrderAddedMessage a{};
      a.TimestampNanoseconds = ns;
      a.OrderNumber = 0;
      a.BuySellIndicator = ' ';
      copy4(a.OrderbookId, i.key.data());
      copy4(a.Group, i.key.data() + 4);
      a.Price = i.ref_price;
      put(out, a);
    }
    auto order = [&](const Order &o) {
      const Instrument &i = *o.inst;
      if (o.order_type != ' ')
      {
        G::OrderAddedWithAttributesMessage f{};
        f.TimestampNanoseconds = ns;
        f.OrderNumber = o.number;
        f.BuySellIndicator = o.side;
        f.Quantity = o.qty;
        copy4(f.OrderbookId, i.key.data());
        copy4(f.Group, i.key.data() + 4);
        f.Price = o.px;
        f.set_Attribution("");
        f.OrderType = o.order_type;
        put(out, f);
      }
      else
      {
        G::OrderAddedMessage a{};
        a.TimestampNanoseconds = ns;
        a.OrderNumber = o.number;
        a.BuySellIndicator = o.side;
        a.Quantity = o.qty;
        copy4(a.OrderbookId, i.key.data());
        copy4(a.Group, i.key.data() + 4);
        a.Price = o.px;
        put(out, a);
      }
    };
    for (const auto &ip : ins.all())
    {
      for (const auto &[px, l] : ip->book.bids)
        for (const Order *o : l.q)
          order(*o);
      for (const auto &[px, l] : ip->book.asks)
        for (const Order *o : l.q)
          order(*o);
    }
    {
      G::EndOfSnapshotMessage g{};
      g.SequenceNumber = next_seq;
      put(out, g);
    }
    return out;
  }
}
