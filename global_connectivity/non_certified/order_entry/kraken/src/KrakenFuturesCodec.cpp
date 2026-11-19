/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_oe/KrakenFuturesCodec.hpp"

#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"

namespace kraken::oe::futures
{
  using json = nlohmann::json;

  namespace
  {
    std::string text(const json &j, const char *k)
    {
      auto it = j.find(k);
      if (it == j.end() || it->is_null())
        return {};
      if (it->is_string())
        return it->get<std::string>();
      return kraken::num_text(*it);
    }

    void param(std::string &b, const char *k, const std::string &v)
    {
      if (!b.empty())
        b += '&';
      b += k;
      b += '=';
      b += url_encode(v);
    }

    const char *order_type(const NewOrder &o)
    {
      if (o.market)
        return "mkt";
      if (o.post_only)
        return "post";
      switch (o.tif)
      {
      case Tif::IOC: return "ioc";
      case Tif::FOK: return "fok";
      case Tif::GTC: break;
      }
      return "lmt";
    }

    const json &object_or_empty(const json &j, const char *k)
    {
      static const json empty = json::object();
      auto it = j.find(k);
      return it != j.end() && it->is_object() ? *it : empty;
    }
  }

  std::string send_order_body(const NewOrder &o)
  {
    std::string b;
    param(b, "orderType", order_type(o));
    param(b, "symbol", o.symbol);
    param(b, "side", o.buy ? "buy" : "sell");
    param(b, "size", o.qty);
    if (!o.market)
      param(b, "limitPrice", o.price);
    if (!o.cl_ord_id.empty())
      param(b, "cliOrdId", o.cl_ord_id);
    return b;
  }

  std::string cancel_order_body(const std::string &order_id, const std::string &cl_ord_id)
  {
    std::string b;
    if (!order_id.empty())
      param(b, "order_id", order_id);
    else
      param(b, "cliOrdId", cl_ord_id);
    return b;
  }

  std::string orders_status_body(const std::vector<std::string> &order_ids, const std::vector<std::string> &cl_ord_ids)
  {
    std::string b;
    for (const auto &i : order_ids)
      param(b, "orderIds", i);
    for (const auto &c : cl_ord_ids)
      param(b, "cliOrdIds", c);
    return b;
  }

  std::string error_of(const json &resp)
  {
    if (resp.value("result", std::string()) != "error")
      return {};
    std::string e = text(resp, "error");
    if (e.empty())
      if (auto es = resp.find("errors"); es != resp.end() && es->is_array() && !es->empty())
        e = es->front().is_string() ? es->front().get<std::string>() : es->front().dump();
    return e.empty() ? std::string("error") : e;
  }

  std::vector<Report> parse_send(const json &resp, const std::string &cl_ord_id)
  {
    Report r;
    r.cl_ord_id = cl_ord_id;
    if (auto e = error_of(resp); !e.empty())
    {
      r.kind = Report::Rejected;
      r.reason = e;
      return {r};
    }
    const json &st = object_or_empty(resp, "sendStatus");
    r.order_id = text(st, "order_id");
    if (r.cl_ord_id.empty())
      r.cl_ord_id = text(st, "cliOrdId");
    r.status = text(st, "status");
    r.time_ns = kraken::parse_rfc3339(text(st, "receivedTime"));
    if (r.status == "placed" || r.status == "partiallyFilled" || r.status == "filled")
      r.kind = Report::Accepted;
    else
    {
      r.kind = Report::Rejected;
      r.reason = r.status.empty() ? "no sendStatus.status" : r.status;
      // A REJECT event names the reason in Kraken's own words.
      if (auto ev = st.find("orderEvents"); ev != st.end() && ev->is_array())
        for (const auto &e : *ev)
          if (e.value("type", std::string()) == "REJECT" && e.contains("reason"))
            r.reason += " (" + text(e, "reason") + ")";
    }
    return {r};
  }

  std::vector<Report> parse_cancel(const json &resp, const std::string &cl_ord_id)
  {
    Report r;
    r.cl_ord_id = cl_ord_id;
    if (auto e = error_of(resp); !e.empty())
    {
      r.kind = Report::CancelRejected;
      r.reason = e;
      return {r};
    }
    const json &st = object_or_empty(resp, "cancelStatus");
    r.order_id = text(st, "order_id");
    r.status = text(st, "status");
    r.time_ns = kraken::parse_rfc3339(text(st, "receivedTime"));
    if (r.status == "cancelled")
      r.kind = Report::Canceled;
    else
    {
      // filled: the fills will end the order; notFound: orders/status says
      // what happened to it.
      r.kind = Report::CancelRejected;
      r.reason = r.status.empty() ? "no cancelStatus.status" : r.status;
    }
    return {r};
  }

  std::vector<Report> parse_fills(const json &resp)
  {
    std::vector<Report> out;
    auto f = resp.find("fills");
    if (f == resp.end() || !f->is_array())
      return out;
    for (const auto &e : *f)
    {
      Report r;
      r.kind = Report::Trade;
      r.exec_id = text(e, "fill_id");
      r.trade_id = text(e, "sequence_id");
      r.order_id = text(e, "order_id");
      r.cl_ord_id = text(e, "cliOrdId");
      r.symbol = text(e, "symbol");
      r.buy = text(e, "side") != "sell";
      r.last_qty = text(e, "size");
      r.last_price = text(e, "price");
      r.maker = text(e, "fillType") == "maker";
      r.status = text(e, "fillType");
      r.time_ns = kraken::parse_rfc3339(text(e, "fillTime"));
      out.push_back(std::move(r));
    }
    return out;
  }

  std::vector<Report> parse_orders_status(const json &resp)
  {
    std::vector<Report> out;
    auto os = resp.find("orders");
    if (os == resp.end() || !os->is_array())
      return out;
    for (const auto &e : *os)
    {
      const json &o = object_or_empty(e, "order");
      Report r;
      r.order_id = text(o, "orderId");
      r.cl_ord_id = text(o, "cliOrdId");
      r.symbol = text(o, "symbol");
      r.buy = text(o, "side") != "sell";
      r.status = text(e, "status");
      r.reason = text(e, "updateReason");
      if (auto err = text(e, "error"); !err.empty())
        r.reason += (r.reason.empty() ? "" : " ") + err;
      r.time_ns = kraken::parse_rfc3339(text(o, "lastUpdateTimestamp"));
      if (r.status == "ENTERED_BOOK")
        r.kind = Report::Accepted;
      else if (r.status == "FULLY_EXECUTED")
        r.kind = Report::Filled;
      else if (r.status == "CANCELLED")
        r.kind = Report::Canceled;
      else if (r.status == "REJECTED")
        r.kind = Report::Rejected;
      else
        r.kind = Report::Other; // trigger orders: not sent by this OM
      out.push_back(std::move(r));
    }
    return out;
  }
}
