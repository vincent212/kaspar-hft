/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_oe/KrakenSpotCodec.hpp"

#include "kraken_md/KrakenJson.hpp"

namespace kraken::oe::spot
{
  using json = nlohmann::json;

  namespace
  {
    std::string q(const std::string &s) { return json(s).dump(); }

    const char *tif(Tif t)
    {
      switch (t)
      {
      case Tif::GTC: return "gtc";
      case Tif::IOC: return "ioc";
      case Tif::FOK: return "fok";
      }
      return "gtc";
    }

    std::string text(const json &j, const char *k)
    {
      auto it = j.find(k);
      if (it == j.end() || it->is_null())
        return {};
      if (it->is_string())
        return it->get<std::string>();
      return kraken::num_text(*it);
    }

    Report execution(const json &d, bool snapshot)
    {
      Report r;
      r.snapshot = snapshot;
      r.order_id = text(d, "order_id");
      r.cl_ord_id = text(d, "cl_ord_id");
      r.exec_id = text(d, "exec_id");
      r.trade_id = text(d, "trade_id");
      r.symbol = text(d, "symbol");
      r.buy = text(d, "side") != "sell";
      r.last_qty = text(d, "last_qty");
      r.last_price = text(d, "last_price");
      r.reason = text(d, "reason");
      if (r.reason.empty())
        r.reason = text(d, "cancel_reason");
      r.status = text(d, "order_status");
      r.maker = text(d, "liquidity_ind") == "m";
      r.time_ns = kraken::parse_rfc3339(text(d, "timestamp"));
      const std::string et = text(d, "exec_type");
      if (et == "new")
        r.kind = Report::Accepted;
      else if (et == "trade")
        r.kind = Report::Trade;
      else if (et == "filled")
        r.kind = Report::Filled;
      else if (et == "canceled" || et == "expired")
        r.kind = Report::Canceled;
      else if (et == "amended" || et == "restated")
        r.kind = Report::Restated;
      else if (et.empty() && snapshot && (r.status == "new" || r.status == "partially_filled"))
        r.kind = Report::Accepted; // an open order in the snapshot
      else
        r.kind = Report::Other; // pending_new, iceberg_refill, status
      return r;
    }
  }

  std::string add_order(const NewOrder &o, const std::string &token, uint64_t req_id)
  {
    std::string p = R"({"method":"add_order","params":{"order_type":)" + q(o.market ? "market" : "limit") +
                    R"(,"side":)" + q(o.buy ? "buy" : "sell") + R"(,"order_qty":)" + o.qty + R"(,"symbol":)" +
                    q(o.symbol);
    if (!o.market)
      p += R"(,"limit_price":)" + o.price;
    p += R"(,"time_in_force":)" + q(tif(o.tif));
    if (o.post_only)
      p += R"(,"post_only":true)";
    if (!o.cl_ord_id.empty())
      p += R"(,"cl_ord_id":)" + q(o.cl_ord_id);
    p += R"(,"token":)" + q(token) + R"(},"req_id":)" + std::to_string(req_id) + "}";
    return p;
  }

  std::string cancel_order(const std::string &order_id, const std::string &cl_ord_id, const std::string &token,
                           uint64_t req_id)
  {
    json params = {{"token", token}};
    if (!order_id.empty())
      params["order_id"] = json::array({order_id});
    else
      params["cl_ord_id"] = json::array({cl_ord_id});
    return json{{"method", "cancel_order"}, {"params", params}, {"req_id", req_id}}.dump();
  }

  std::string subscribe_executions(const std::string &token, bool snap_orders, bool snap_trades)
  {
    return json{{"method", "subscribe"},
                {"params",
                 {{"channel", "executions"},
                  {"token", token},
                  {"snap_orders", snap_orders},
                  {"snap_trades", snap_trades},
                  {"order_status", true}}}}
        .dump();
  }

  std::string cancel_after(int timeout_s, const std::string &token)
  {
    return json{{"method", "cancel_all_orders_after"}, {"params", {{"timeout", timeout_s}, {"token", token}}}}.dump();
  }

  Parsed parse(const json &msg, const ReqLookup &lookup)
  {
    Parsed out;
    if (auto m = msg.find("method"); m != msg.end() && m->is_string())
    {
      const std::string method = m->get<std::string>();
      const bool ok = msg.value("success", false);
      const uint64_t req = msg.value("req_id", uint64_t(0));
      const json res = msg.value("result", json::object());
      if (method == "add_order")
      {
        Report r;
        r.cl_ord_id = text(res, "cl_ord_id");
        if (r.cl_ord_id.empty() && lookup)
          r.cl_ord_id = lookup(req);
        r.order_id = text(res, "order_id");
        r.kind = ok ? Report::Accepted : Report::Rejected;
        r.reason = text(msg, "error");
        if (!r.cl_ord_id.empty() || !r.order_id.empty())
          out.reports.push_back(r);
      }
      else if (method == "cancel_order" && !ok)
      {
        Report r;
        r.kind = Report::CancelRejected;
        r.cl_ord_id = lookup ? lookup(req) : std::string();
        r.reason = text(msg, "error");
        out.reports.push_back(r);
      }
      else if (method == "subscribe" && res.value("channel", std::string()) == "executions")
      {
        if (ok)
          out.subscribed_executions = true;
        else
          out.subscribe_error = text(msg, "error");
      }
      else if (method == "subscribe" && !ok)
        out.subscribe_error = text(msg, "error");
      return out;
    }
    if (msg.value("channel", std::string()) != "executions")
      return out;
    if (auto s = msg.find("sequence"); s != msg.end() && s->is_number())
      out.sequence = s->get<uint64_t>();
    const bool snapshot = msg.value("type", std::string()) == "snapshot";
    out.snapshot = snapshot;
    if (auto d = msg.find("data"); d != msg.end() && d->is_array())
      for (const auto &e : *d)
        out.reports.push_back(execution(e, snapshot));
    return out;
  }
}
