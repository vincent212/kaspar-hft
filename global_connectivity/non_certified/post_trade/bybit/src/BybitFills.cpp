/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_post_trade/BybitFills.hpp"

#include <cctype>
#include <cmath>
#include <set>

namespace bybit_pt
{

ExecKind classify(const std::string &t)
{
  static const std::set<std::string> position = {"Trade",        "AdlTrade",     "BustTrade",
                                                 "Delivery",     "BlockTrade",   "MovePosition",
                                                 "FutureSpread"};
  static const std::set<std::string> cash = {"Funding", "Settle"};
  if (position.count(t))
    return ExecKind::Position;
  if (cash.count(t))
    return ExecKind::Cash;
  return ExecKind::Unknown;
}

en::x venue_of(const std::string &c)
{
  if (c == "spot")
    return en::x::BYBIT;
  if (c == "linear")
    return en::x::BYBITF;
  return en::x{};
}

const char *to_string(Parse p)
{
  switch (p)
  {
  case Parse::Ok: return "Ok";
  case Parse::NotATrade: return "NotATrade";
  case Parse::NoInstrument: return "NoInstrument";
  case Parse::BadQuantity: return "BadQuantity";
  case Parse::Malformed: return "Malformed";
  }
  return "?";
}

namespace
{
  std::string s(const nlohmann::json &e, const char *k)
  {
    auto it = e.find(k);
    if (it == e.end() || it->is_null())
      return {};
    return it->is_string() ? it->get<std::string>() : it->dump();
  }

  // days since 1970-01-01 -> YYYYMMDD (Howard Hinnant's civil_from_days)
  uint32_t yyyymmdd(int64_t z)
  {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned mm = mp < 10 ? mp + 3 : mp - 9;
    return uint32_t((y + (mm <= 2)) * 10000 + mm * 100 + dd);
  }
}

Parse to_capture(const nlohmann::json &e, const std::string &category_in, const Instruments &ins,
                 bool from_rest, post_trade::msg::TradeCapture &out, std::string &why)
{
  const std::string type = s(e, "execType");
  if (classify(type) != ExecKind::Position)
  {
    why = "execType " + type;
    return Parse::NotATrade;
  }
  const std::string cat = e.contains("category") ? s(e, "category") : category_in;
  const std::string sym = s(e, "symbol");
  const std::string id = s(e, "execId");
  const std::string side = s(e, "side");
  const std::string tms = s(e, "execTime");
  if (id.empty() || sym.empty() || tms.empty() || (side != "Buy" && side != "Sell"))
  {
    why = "execId, symbol, execTime or side missing";
    return Parse::Malformed;
  }
  auto it = ins.find({cat, sym});
  if (it == ins.end())
  {
    why = cat + " " + sym + " is not configured";
    return Parse::NoInstrument;
  }
  const Instrument &in = it->second;

  int64_t pm = 0, qm = 0, tm = 0;
  int pd = 0, qd = 0, td = 0;
  if (!parse_decimal(s(e, "execPrice"), pm, pd) || !parse_decimal(s(e, "execQty"), qm, qd) ||
      !parse_decimal(tms, tm, td) || td != 0 || pm < 0 || qm < 0 || tm < 0)
  {
    why = "execPrice, execQty or execTime not a non-negative decimal";
    return Parse::Malformed;
  }
  const double q = double(qm) * std::pow(10.0, -qd);
  const double n = q / in.qty_unit;
  const int64_t units = std::llround(n);
  if (units <= 0 || std::fabs(n - double(units)) > 1e-6)
  {
    why = "execQty " + s(e, "execQty") + " is not a whole number of " + std::to_string(in.qty_unit);
    return Parse::BadQuantity;
  }

  out = post_trade::msg::TradeCapture{};
  out.venue = venue_of(cat);
  out.trade_id = id;
  out.report_id = id;
  out.report_seq = 0;
  out.event = post_trade::Event::New;
  out.venue_order_id = s(e, "orderId");
  out.symbol = sym;
  out.asset = in.asset;
  out.side = side == "Buy" ? post_trade::Side::Buy : post_trade::Side::Sell;
  out.qty = units;
  out.price_mantissa = pm;
  out.price_decimals = pd;
  const int64_t ms = tm;
  out.trade_date = yyyymmdd(ms / 86400000);
  out.trade_time_ns = (ms % 86400000) * 1000000;
  out.account = s(e, "orderLinkId");
  out.poss_dup = from_rest;
  out.raw = e.dump();
  return Parse::Ok;
}

}
