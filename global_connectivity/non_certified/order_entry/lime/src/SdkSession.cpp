/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SdkSession: LimeSession over the Lime C++ Trading API (LimeBrokerage::
// TradingApi). Compiled only when LIME_SDK_HEADER names the header to
// include; otherwise sdk_session_factory() throws.
//
// The tests build it against sdk_doc/LimeTradingApiDoc.hh, a transcription
// of the public overview, with tests/SdkStub.cpp standing in for the
// library. It has never been compiled against the vendor's SDK.
//
// One TradingApi per session object. It is constructed in the factory
// (LimeOM's thread) and destroyed in ~SdkSession (the same thread), as the
// API requires (TO 2). The Listener callbacks run on the API's callback
// thread and only forward to SessionEvents, which copies and posts.

#include <stdexcept>

#include "lime_om/LimeApi.hpp"

#ifdef LIME_SDK_HEADER
#include LIME_SDK_HEADER

namespace lime
{
  namespace sdk = LimeBrokerage;

  namespace
  {
    sdk::Side to_sdk(Side s)
    {
      switch (s)
      {
      case Side::Buy: return sdk::sideBuy;
      case Side::Sell: return sdk::sideSell;
      case Side::SellShort: return sdk::sideSellShort;
      case Side::SellShortExempt: return sdk::sideSellShortExempt;
      case Side::BuyToCover: return sdk::sideBuyToCover;
      }
      return sdk::sideBuy;
    }

    Side from_sdk(sdk::Side s)
    {
      switch (s)
      {
      case sdk::sideBuy: return Side::Buy;
      case sdk::sideSell: return Side::Sell;
      case sdk::sideSellShort: return Side::SellShort;
      case sdk::sideSellShortExempt: return Side::SellShortExempt;
      case sdk::sideBuyToCover: return Side::BuyToCover;
      }
      return Side::Buy;
    }

    sdk::OrderProperties::TimeInForce tif_order(Tif t)
    {
      using P = sdk::OrderProperties;
      switch (t)
      {
      case Tif::Day: return P::timeInForceDay;
      case Tif::Opg: return P::timeInForceOpg;
      case Tif::Ioc: return P::timeInForceIoc;
      case Tif::ExtendedDay: return P::timeInForceExtendedDay;
      case Tif::AtTheClose: return P::timeInForceAtTheClose;
      }
      return P::timeInForceDay;
    }

    sdk::AlgoOrderProperties::TimeInForce tif_algo(Tif t)
    {
      using P = sdk::AlgoOrderProperties;
      switch (t)
      {
      case Tif::Day: return P::timeInForceDay;
      case Tif::Opg: return P::timeInForceOpg;
      case Tif::Ioc: return P::timeInForceIoc;
      case Tif::ExtendedDay: return P::timeInForceExtendedDay;
      case Tif::AtTheClose: return P::timeinForceAtTheClose; // sic (TO 4.3)
      }
      return P::timeInForceDay;
    }

    CallStatus from_sdk(sdk::CallStatus s)
    {
      switch (s)
      {
      case sdk::statusSuccess: return CallStatus::Success;
      case sdk::statusConnectionBusy: return CallStatus::ConnectionBusy;
      default: return CallStatus::ConnectionError;
      }
    }

    ExecType from_sdk(sdk::ExecType t)
    {
      switch (t)
      {
      case sdk::execTypeFill: return ExecType::Fill;
      case sdk::execTypeBust: return ExecType::Bust;
      case sdk::execTypeCorrection: return ExecType::Correction;
      }
      return ExecType::Correction; // unknown: never treated as a fill
    }
  }

  class SdkSession : public LimeSession, public sdk::Listener
  {
  public:
    SdkSession(const LoginParams &p, SessionEvents &ev) : ev_(ev)
    {
      try
      {
        api_ = new sdk::TradingApi(*this, p.account, p.user, p.password, p.event_id, p.cancel_all_on_disconnect,
                                   p.host.empty() ? sdk::nullHostname : p.host,
                                   p.infiniband ? sdk::transportTypeInfiniband : sdk::transportTypeTcp);
      }
      catch (const sdk::TradingApiException &e)
      {
        throw std::runtime_error(std::string("Lime TradingApi: ") + e.what());
      }
    }
    ~SdkSession() override { delete api_; }

    CallStatus place(const NewOrder &o) override
    {
      try
      {
        if (o.strategy.empty())
        {
          sdk::OrderProperties p;
          p.setTimeInForce(tif_order(o.tif));
          if (o.sale_affirm)
            p.setSaleAffirm(true);
          if (o.regular_session_only)
            p.setRegularSessionOnly(true);
          if (!o.client_data1.empty())
            p.setClientData1(o.client_data1);
          return from_sdk(api_->placeOrder(o.id, o.route, o.symbol, to_sdk(o.side), o.quantity,
                                           o.price > 0 ? o.price : sdk::marketPrice, p));
        }
        sdk::AlgoOrderProperties p;
        p.setTimeInForce(tif_algo(o.tif));
        if (o.sale_affirm)
          p.setSaleAffirm(true);
        if (o.regular_session_only)
          p.setRegularSessionOnly(true);
        if (!o.client_data1.empty())
          p.setClientData1(o.client_data1);
        return from_sdk(api_->placeAlgoOrder(o.id, o.route, o.symbol, to_sdk(o.side), o.quantity, o.strategy,
                                             o.price > 0 ? o.price : sdk::marketPrice, p));
      }
      catch (const sdk::TradingApiException &e)
      {
        throw std::runtime_error(std::string("Lime placeOrder: ") + e.what());
      }
    }

    CallStatus cancel(OrderId id) override
    {
      try
      {
        return from_sdk(api_->cancelOrder(id));
      }
      catch (const sdk::TradingApiException &e)
      {
        throw std::runtime_error(std::string("Lime cancelOrder: ") + e.what());
      }
    }

    CallStatus cancel_all() override
    {
      try
      {
        return from_sdk(api_->cancelAllOpenOrders());
      }
      catch (const sdk::TradingApiException &e)
      {
        throw std::runtime_error(std::string("Lime cancelAllOpenOrders: ") + e.what());
      }
    }

    // ---- Listener: the API's callback thread ----
    void onOrderAccept(sdk::OrderId id, sdk::OrderId lime_id, const sdk::OrderAckAttributes &,
                       uint64_t ev) override
    {
      ev_.accepted(id, lime_id, ev);
    }
    void onOrderFill(sdk::OrderId id, const sdk::FillInfo &f, uint64_t ev) override
    {
      FillInfo x;
      x.symbol = f.symbol;
      x.side = from_sdk(f.side);
      x.last_shares = f.lastShares;
      x.last_price = f.lastPrice;
      x.left_qty = f.leftQty;
      x.liquidity = f.liquidity;
      x.transact_time_ms = f.transactTime;
      x.transact_time_ns = f.transactTimeNanos;
      x.exec_type = from_sdk(f.execType);
      x.exec_id = f.execId;
      x.last_market = f.lastMarket;
      ev_.filled(id, x, ev);
    }
    void onUSOptionsOrderFill(sdk::OrderId id, const sdk::USOptionsFillInfo &f, uint64_t ev) override
    {
      // This OM places no options orders: passed on as a correction, which
      // the OM logs and never applies.
      FillInfo x;
      x.last_shares = f.lastShares;
      x.exec_type = ExecType::Correction;
      x.exec_id = "options fill";
      ev_.filled(id, x, ev);
    }
    void onOrderCancel(sdk::OrderId id, uint64_t ev) override { ev_.canceled(id, std::string(), ev); }
    void onOrderCancel(sdk::OrderId id, const std::string &reason, uint64_t ev) override
    {
      ev_.canceled(id, reason, ev);
    }
    void onOrderPartialCancel(sdk::OrderId id, int left, uint64_t ev) override { ev_.partial_canceled(id, left, ev); }
    void onOrderReplace(sdk::OrderId id, sdk::OrderId rid, sdk::OrderId lime_id, const sdk::OrderAckAttributes &,
                        uint64_t ev) override
    {
      ev_.replaced(id, rid, lime_id, ev);
    }
    void onOrderReject(sdk::OrderId id, const std::string &reason, uint64_t ev) override
    {
      ev_.rejected(id, reason, ev);
    }
    void onOrderCancelReject(sdk::OrderId id, const std::string &reason, uint64_t ev) override
    {
      ev_.cancel_rejected(id, reason, ev);
    }
    void onOrderCancelReplaceReject(sdk::OrderId id, sdk::OrderId rid, const std::string &reason,
                                    uint64_t ev) override
    {
      ev_.replace_rejected(id, rid, reason, ev);
    }
    void onManualOrder(sdk::OrderId id, const sdk::ManualOrderInfo &i, uint64_t ev) override
    {
      ev_.manual_order(id, i.symbol, i.route, i.quantity, i.price, from_sdk(i.side), ev);
    }
    void onManualOrderReplace(sdk::OrderId id, sdk::OrderId rid, int qty, sdk::ScaledPrice px, uint64_t ev) override
    {
      ev_.manual_replace(id, rid, qty, px, ev);
    }
    void onLoginAccepted() override { ev_.login_accepted(0); }
    void onLoginAccepted(uint64_t ev) override { ev_.login_accepted(ev); }
    void onLoginFailed(const std::string &reason) override { ev_.login_failed(reason); }
    void onConnectionFailed(const std::string &reason) override { ev_.connection_failed(reason); }
    void onConnectionBusy() override { ev_.busy(); }
    void onConnectionAvailable() override { ev_.available(); }

  private:
    SessionEvents &ev_;
    sdk::TradingApi *api_ = nullptr;
  };

  bool sdk_available() { return true; }

  SessionFactory sdk_session_factory()
  {
    return [](const LoginParams &p, SessionEvents &ev) -> std::unique_ptr<LimeSession> {
      return std::make_unique<SdkSession>(p, ev);
    };
  }
}

#else

namespace lime
{
  bool sdk_available() { return false; }

  SessionFactory sdk_session_factory()
  {
    return [](const LoginParams &, SessionEvents &) -> std::unique_ptr<LimeSession> {
      throw std::runtime_error("built without the Lime SDK (LIME_SDK_HEADER)");
    };
  }
}

#endif
