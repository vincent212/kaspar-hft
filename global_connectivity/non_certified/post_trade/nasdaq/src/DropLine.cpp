/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_post_trade/DropLine.hpp"

#include <cstdio>
#include <cstring>

#include "post_trade/EasternTime.hpp"

namespace nasdaq_post_trade
{
  namespace
  {
    std::string_view trim(std::string_view s)
    {
      while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
      while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
      return s;
    }

    template <size_t N>
    std::string_view field(const char (&f)[N]) { return std::string_view(f, N); }

    bool fail(std::string *err, std::string why)
    {
      if (err) *err = std::move(why);
      return false;
    }

    // Byte offsets of the commas: every Sep<offset> member of Line.
    constexpr size_t COMMAS[] = {9, 11, 18, 34, 45, 47, 54, 63, 75, 80, 93, 106, 108, 110, 112, 124};
  }

  bool parse_uint(std::string_view s, uint64_t &v)
  {
    s = trim(s);
    v = 0;
    for (char c : s)
    {
      if (c < '0' || c > '9') return false;
      v = v * 10 + uint64_t(c - '0');
    }
    return true; // all spaces reads as 0
  }

  bool parse_price(std::string_view s, int64_t &e4)
  {
    s = trim(s);
    if (s.empty()) return false;
    auto dot = s.find('.');
    std::string_view whole = s.substr(0, dot);
    std::string_view frac = dot == std::string_view::npos ? std::string_view() : s.substr(dot + 1);
    if (frac.size() > 4) return false;
    uint64_t w = 0, f = 0;
    if (!whole.empty() && (trim(whole).size() != whole.size() || !parse_uint(whole, w))) return false;
    if (!frac.empty() && (trim(frac).size() != frac.size() || !parse_uint(frac, f))) return false;
    for (size_t i = frac.size(); i < 4; ++i) f *= 10;
    e4 = int64_t(w) * 10000 + int64_t(f);
    return true;
  }

  bool parse_time(std::string_view s, int64_t &ns)
  {
    s = trim(s);
    if (s.empty()) return false;
    auto dot = s.find('.');
    std::string_view whole = s.substr(0, dot);
    std::string_view frac = dot == std::string_view::npos ? std::string_view() : s.substr(dot + 1);
    if (whole.empty() || frac.size() > 9) return false;
    uint64_t w = 0, f = 0;
    if (!parse_uint(whole, w) || trim(whole).size() != whole.size()) return false;
    if (!frac.empty() && (!parse_uint(frac, f) || trim(frac).size() != frac.size())) return false;
    for (size_t i = frac.size(); i < 9; ++i) f *= 10;
    if (w >= 86400) return false;
    ns = int64_t(w) * 1'000'000'000 + int64_t(f);
    return true;
  }

  std::string format_price(int64_t e4)
  {
    char b[32];
    std::snprintf(b, sizeof b, "%lld.%04lld", (long long)(e4 / 10000), (long long)(e4 % 10000));
    return b;
  }

  std::string format_time(int64_t ns)
  {
    char b[32];
    std::snprintf(b, sizeof b, "%lld.%03lld", (long long)(ns / 1'000'000'000),
                  (long long)(ns % 1'000'000'000 / 1'000'000));
    return b;
  }

  bool parse_line(std::string_view bytes, DropReport &r, std::string *err)
  {
    if (bytes.size() > LINE_SIZE && bytes.back() == '\n')
    {
      bytes.remove_suffix(1);
      if (bytes.size() > LINE_SIZE && bytes.back() == '\r')
        bytes.remove_suffix(1);
    }
    if (bytes.size() != LINE_SIZE)
      return fail(err, "line is " + std::to_string(bytes.size()) + " bytes, expected " +
                           std::to_string(LINE_SIZE) + " (plus CR/LF)");
    for (size_t c : COMMAS)
      if (bytes[c] != ',')
        return fail(err, "no comma at offset " + std::to_string(c));

    Line l;
    std::memcpy(&l, bytes.data(), sizeof l);

    r = DropReport{};
    r.type = l.Type;
    if (!parse_time(field(l.TimeStamp), r.time_ns))
      return fail(err, "bad TimeStamp '" + std::string(field(l.TimeStamp)) + "'");
    r.source = l.get_Source();
    r.order_token = l.get_OrderToken();
    r.replaced_token = l.get_ReplacedToken();
    r.side = l.BuySell;
    uint64_t u = 0;
    if (!parse_uint(field(l.Shares), u))
      return fail(err, "bad Shares '" + std::string(field(l.Shares)) + "'");
    r.shares = int64_t(u);
    r.stock = l.get_Stock();
    if (!parse_price(field(l.Price), r.price_e4))
      return fail(err, "bad Price '" + std::string(field(l.Price)) + "'");
    r.firm = l.get_Firm();
    if (!parse_uint(field(l.Reference), r.reference))
      return fail(err, "bad Reference '" + std::string(field(l.Reference)) + "'");
    if (!parse_uint(field(l.MatchTIF), r.match_tif))
      return fail(err, "bad Match/TIF '" + std::string(field(l.MatchTIF)) + "'");
    r.capacity = l.Capacity;
    r.liquidity = l.LiquidityCode;
    r.clearing = l.ClearingCode;
    // "may be 0 if not applicable" (p.7); blank is read as 0 as well
    if (trim(field(l.ReferencePrice)).empty())
      r.ref_price_e4 = 0;
    else if (!parse_price(field(l.ReferencePrice), r.ref_price_e4))
      return fail(err, "bad Reference Price '" + std::string(field(l.ReferencePrice)) + "'");
    r.ref_price_type = l.ReferencePriceType;
    return true;
  }

  std::string format_line(const DropReport &r, bool crlf)
  {
    Line l;
    std::memset(&l, ' ', sizeof l);
    for (size_t c : COMMAS)
      reinterpret_cast<char *>(&l)[c] = ',';
    l.set_TimeStamp(format_time(r.time_ns));
    l.Type = r.type;
    l.set_Source(r.source);
    l.set_OrderToken(r.order_token);
    l.set_ReplacedToken(r.replaced_token);
    l.BuySell = r.side;
    l.set_Shares(std::to_string(r.shares));
    l.set_Stock(r.stock);
    l.set_Price(format_price(r.price_e4));
    l.set_Firm(r.firm);
    l.set_Reference(std::to_string(r.reference));
    l.set_MatchTIF(std::to_string(r.match_tif));
    l.Capacity = r.capacity;
    l.LiquidityCode = r.liquidity;
    l.ClearingCode = r.clearing;
    l.set_ReferencePrice(format_price(r.ref_price_e4));
    l.ReferencePriceType = r.ref_price_type;
    std::string s(reinterpret_cast<const char *>(&l), sizeof l);
    if (crlf) s += "\r\n";
    return s;
  }

  std::string trade_id(const DropReport &r)
  {
    return std::to_string(r.match_tif) + "." + std::to_string(r.reference);
  }

  std::optional<post_trade::msg::TradeCapture>
  to_trade_capture(const DropReport &r, std::string_view raw, const MapContext &c)
  {
    using post_trade::Event;
    using post_trade::Side;
    post_trade::msg::TradeCapture t;
    switch (DropType(r.type))
    {
    case DropType::Executed: t.event = Event::New; break;
    case DropType::Correction: t.event = Event::Amend; break;
    case DropType::Broken: t.event = Event::Bust; break;
    default: return std::nullopt;
    }
    t.venue = en::x::NASDAQ;
    t.trade_id = trade_id(r);
    if (t.event != Event::New) t.ref_trade_id = t.trade_id;
    t.report_id = c.session + ":" + std::to_string(c.seq);
    t.report_seq = c.seq;
    t.venue_order_id = std::to_string(r.reference);
    t.symbol = r.stock;
    if (c.assets)
      if (auto it = c.assets->find(r.stock); it != c.assets->end())
        t.asset = it->second;
    switch (r.side)
    {
    case 'B': t.side = Side::Buy; break;
    case 'S': t.side = Side::Sell; break;
    case 'T': t.side = Side::SellShort; break;
    case 'E': t.side = Side::SellShortExempt; break;
    default: t.side = Side::Unknown; break;
    }
    t.qty = r.shares;
    t.price_mantissa = r.price_e4;
    t.price_decimals = 4;
    t.trade_date = c.trade_date;
    t.trade_time_ns = r.time_ns;
    t.settle_date = 0; // not in DROP
    t.account = r.source;
    t.firm = r.firm;
    t.poss_dup = c.poss_dup;
    t.raw = std::string(raw);
    return t;
  }

  uint32_t eastern_date(int64_t utc_seconds) { return post_trade::eastern_time::date(utc_seconds); }
}
