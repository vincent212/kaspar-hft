#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <memory>

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "ttom/msg/ExecutionReport.hpp"
#include "ttom/msg/OrderCancelReject.hpp"
#include "ttom/TTFixClient.hpp"
#include "ttom/CallBackImp.hpp"
#include "ttom/ExecType.hpp"
#include "ttom/OrdStatus.hpp"

#include "config.h"
#include "quickfix/FileStore.h"
#include "quickfix/SocketInitiator.h"
#include "quickfix/SSLSocketInitiator.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/Log.h"

namespace ttfixom::act
{
  class TTFIXOM : public actors::Actor
  {
  public:
    TTFIXOM(
        const std::string &_fixConfig,
        actors::Actor *_som = nullptr,
        en::x _venue = en::x::TT)
        : fixConfig(_fixConfig),
          session_settings(_fixConfig),
          storeFactory(session_settings),
          logFactory(session_settings),
          som(_som),
          venue(_venue)

    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(ttfixom::msg::ExecutionReport, executionreport_handler);
      MESSAGE_HANDLER(ttfixom::msg::OrderCancelReject, ordercancelreject_handler);
      MESSAGE_HANDLER(frame::som::msg::Order, order_handler);
      MESSAGE_HANDLER(frame::som::msg::Cancel, cancel_handler);

      auto targetCompId = session_settings.get().getString("TARGETCOMPID");
      auto senderCompId = session_settings.get().getString("SENDERCOMPID");
      auto account = session_settings.get().getString("ACCOUNT");
      auto exchange = session_settings.get().getString("SECURITYEXCHANGE");
      auto password = session_settings.get().getString("PASSWORD");
      // OnBehalfOfSubID (tag 116) — optional cfg key; required by TT when an
      // account has multiple users mapped to it. QuickFIX has no native
      // auto-stamp for tag 116, so we read it here and TTFixClient::toApp
      // stamps it on every outbound business message.
      std::string onBehalfOfSubID;
      try { onBehalfOfSubID = session_settings.get().getString("OnBehalfOfSubID"); } catch (...) {}
      cb.reset(new ttfixom::CallBackImp(this));
      client.reset(new ttfixom::TTFixClient(
          cb.get(),
          exchange,
          account,
          password,
          targetCompId,
          senderCompId,
          onBehalfOfSubID));
#ifdef USESSLFIX
      initiator.reset(new FIX::SSLSocketInitiator(
          *client, storeFactory, session_settings, logFactory));
#else
      initiator.reset(new FIX::SocketInitiator(
          *client, storeFactory, session_settings, logFactory));
#endif
      // initiator->start() is deferred to start_handler so the FIX session
      // doesn't begin connecting before the actor framework has fully wired
      // this actor's mailbox/manager.  Otherwise an early onLogon callback
      // (FIX session thread) could race with the actor's init on the
      // manager's thread.
    }

    ~TTFIXOM() = default;  // unique_ptrs handle cb / client / initiator deletion

  private:
    cfsmp som = nullptr;            // SOM to register with (nullptr -> not registered, test mode)
    en::x venue = en::x::TT;        // venue we're the exchange manager for
    std::string fixConfig;
    FIX::SessionSettings session_settings;
    FIX::FileStoreFactory storeFactory;
    FIX::ScreenLogFactory logFactory;
    std::unique_ptr<ttfixom::CallBackImp> cb;
    std::unique_ptr<ttfixom::TTFixClient> client;
    std::unique_ptr<FIX::Initiator> initiator;
    std::map<uint64_t, frame::som::msg::Order> cloid_to_order;
    std::map<uint64_t, frame::som::msg::Cancel> cloid_to_cancel;
    std::map<uint64_t, frame::som::msg::Order> cloid_to_cancel_rep;
    std::map<uint64_t, ttfixom::msg::OrderCancelReject> cloid_to_last_reject;
    std::map<uint64_t, ttfixom::msg::ExecutionReport> cloid_to_last_execrep;
    std::map<int, uint64_t> choid_to_cloid;

    const char *get_name() const override { return "TTFIXOM"; }

    void start_handler(const actors::msg::Start *) noexcept
    {
      // Now that the actor is fully wired (mailbox + manager established by
      // the framework before Start is delivered), kick off the FIX session.
      // Doing this in the ctor would race the framework — an early onLogon
      // callback could fire on the FIX session thread before the actor was
      // ready to handle anything.
      if (initiator)
        initiator->start();

      // Register with the SOM as the exchange manager for our venue, so the
      // SOM routes Order/Cancel messages tagged with this venue to us.
      // Mirrors ilink's arbiter and fenics's OMProxy. Test-mode (som==nullptr)
      // skips registration. TODO: gate on FIX logon to avoid SOM routing to a
      // not-yet-connected gateway.
      if (som)
      {
        log_inf("TTFIXOM registering with SOM for venue: %s", en::to_string(venue));
        som->send(new frame::som::msg::SetExchManager(venue, this), this);
      }
    }

    void executionreport_handler(const ttfixom::msg::ExecutionReport *m) noexcept
    {
      if (m->x == ExecType::NewOrder)
      {
        // send ack
        auto p = cloid_to_order.find(m->clOrdId);
        if (p == cloid_to_order.end())
        {
          log_omg("got ACK for an order system did not place cancelExecType: %s, OrdState: %s, ttoid: %s, cloid: %d, cmeoid: %d",
                  to_string(m->x),
                  to_string(m->s),
                  m->TTOrdId,
                  m->clOrdId,
                  m->cmeOrdId);
          SNGH;
          return;
        }
        const auto &order = p->second;
        log_omg("ExecutionReport (ACK) ExecType: %s, OrdState: %s, choid: %d, cloid: %d, cmeoid: %d",
                to_string(m->x),
                to_string(m->s),
                order.oid,
                m->clOrdId,
                m->cmeOrdId);
        order.sender->send(new frame::som::msg::Ack(order.oid, m->cmeOrdId), this);
      }
      else if (m->x == ExecType::Trade)
      {
        // lookup stbf in cloid_to_order and send fill
        auto p = cloid_to_order.find(m->clOrdId);
        if (p == cloid_to_order.end())
        {
          log_omg("got Trade for an order system did not place cancelExecType: %s, OrdState: %s, ttoid: %s, cloid: %d, cmeoid: %d",
                  to_string(m->x),
                  to_string(m->s),
                  m->TTOrdId,
                  m->clOrdId,
                  m->cmeOrdId);
                    SNGH;
          return;
        }
        auto &order = p->second;
        log_omg("ExecutionReport (TRADE) ExecType: %s, OrdState: %s, choid: %d, cloid: %d, cmeoid: %d",
                to_string(m->x),
                to_string(m->s),
                order.oid,
                m->clOrdId,
                m->cmeOrdId);
        auto amt_filled = order.still_to_be_filled - m->leavesQty;
        order.still_to_be_filled = m->leavesQty;
        order.sender->send(new frame::som::msg::Fill(
            order.oid,
            order.venue,
            order.sym,
            frame::ref::Price((long double)m->px, (uint)order.sym),
            order.side,
            amt_filled,
            order.still_to_be_filled,
            order.owner,
            0 /* tim: TODO populate exchange timestamp */), this);
        // Fully filled — terminal state.  Erase from cloid_to_order to
        // keep the working-orders map from growing unbounded across
        // long sessions.  Mirror in choid_to_cloid.
        if (order.still_to_be_filled == 0) {
          choid_to_cloid.erase(order.oid);
          cloid_to_order.erase(p);
        }
      }
      else if (m->x == ExecType::Canceled)
      {
        // send canc ack
        auto p = cloid_to_cancel.find(m->clOrdId);
        if (p == cloid_to_cancel.end())
        {
          log_omg("got cancel for an order system did not cancel cancelExecType: %s, OrdState: %s, ttoid: %s, cloid: %d, cmeoid: %d",
                  to_string(m->x),
                  to_string(m->s),
                  m->TTOrdId,
                  m->clOrdId,
                  m->cmeOrdId);
          return;
        }
        const auto &canc = p->second;
        log_omg("ExecutionReport (CANCACK) ExecType: %s, OrdState: %s, choid: %d, cloid: %d, cmeoid: %d",
                to_string(m->x),
                to_string(m->s),
                canc.id,
                m->clOrdId,
                m->cmeOrdId);
        canc.sender->send(new frame::som::msg::CancAck(canc.id), this);
        // Canceled — terminal state on both the cancel and the original
        // order.  Drop the cancel entry; also drop the original order.
        // The exec report's clOrdId is the *cancel's* cloid, so we resolve
        // the order's cloid via the canceled order's choid (= canc.id) →
        // choid_to_cloid → cloid_to_order, then erase both.
        cloid_to_cancel.erase(p);
        auto choid_it = choid_to_cloid.find(canc.id);
        if (choid_it != choid_to_cloid.end()) {
          cloid_to_order.erase(choid_it->second);
          choid_to_cloid.erase(choid_it);
        }
      }
      else if (m->x == ExecType::Replaced)
      {
        // move order from cloid_to_cancel_rep to cloid_to_order
        // send canc ack for old order
        // send ack for new order
        auto p = cloid_to_cancel_rep.find(m->clOrdId);
        if (p == cloid_to_cancel_rep.end())
        {
          log_omg("got Replaced for an order system did not replace cancelExecType: %s, OrdState: %s, ttoid: %s, cloid: %d, cmeoid: %d",
                  to_string(m->x),
                  to_string(m->s),
                  m->TTOrdId,
                  m->clOrdId,
                  m->cmeOrdId);
          return;
        }
        cloid_to_order.emplace(std::make_pair(m->cmeOrdId, p->second));
        p->second.sender->send(new frame::som::msg::CancAck(p->second.oid_to_cancel), this);
        p->second.sender->send(new frame::som::msg::Ack(p->second.oid), this);
        ASSERT(int(m->leavesQty) == p->second.still_to_be_filled, "leavesQty is not as requested");
        log_omg("ExecutionReport (REPLCACK) ExecType: %s, OrdState: %s, choid: %d, cloid: %d, cmeoid: %d, oid_to_cancel: %d, leavesQty: %d",
                to_string(m->x),
                to_string(m->s),
                p->second.oid,
                m->clOrdId,
                m->cmeOrdId,
                p->second.oid_to_cancel,
                m->leavesQty);
        cloid_to_cancel_rep.erase(p);
      }
      else if (m->x == ExecType::Rejected)
      {
        // send reject
        auto p = cloid_to_order.find(m->clOrdId);
        if (p == cloid_to_order.end())
        {
          log_omg("got Reject for an order system did not place cancelExecType: %s, OrdState: %s, ttoid: %s, cloid: %d, cmeoid: %d",
                  to_string(m->x),
                  to_string(m->s),
                  m->TTOrdId,
                  m->clOrdId,
                  m->cmeOrdId);
                            SNGH;

          return;
        }
        const auto &order = p->second;
        log_omg("ExecutionReport (REJECT) ExecType: %s, OrdState: %s, choid: %d, cloid: %d, cmeoid: %d, text: %s",
                to_string(m->x),
                to_string(m->s),
                order.oid,
                m->clOrdId,
                m->cmeOrdId,
                m->text);
        log_rej("ExecutionReport (REJECT) ExecType: %s, OrdState: %s, choid: %d, cloid: %d, cmeoid: %d, text: %s",
                to_string(m->x),
                to_string(m->s),
                order.oid,
                m->clOrdId,
                m->cmeOrdId,
                m->text);
        order.sender->send(new frame::som::msg::Reject(order.oid), this);
        // Rejected — terminal.  Drop tracking state.
        choid_to_cloid.erase(order.oid);
        cloid_to_order.erase(p);
      }
      else
        ERR("cant decode ExecutionReport ExecType");
      // Keep the latest exec report per cloid (overwrite, not emplace —
      // emplace is a no-op on duplicate key which would mask later state).
      cloid_to_last_execrep.insert_or_assign(m->clOrdId, *m);
    }

    void ordercancelreject_handler(const ttfixom::msg::OrderCancelReject *m) noexcept
    {
      if (m->cxlRejResponseTo == 1)
      {
        // response to order cancel request
        auto p = cloid_to_cancel.find(m->clOrdId);
        if (p == cloid_to_cancel.end())
        {
          log_omg("got cancel reject for unkown order ttoid: %s, clOrdId: %d",
                  m->TTOrdId, m->clOrdId);
          return;
        }
        const auto &cancmsg = p->second;
        log_omg("OrderCancelReject response to canc s: %s, clOrdId: %d, chid: %d, reason: %d",
                to_string(m->s), m->clOrdId, cancmsg.id, m->cxlRejReason);
        if (m->s == ttfixom::OrdStatus::Filled)
        {
          log_inf("OrderCancelReject response to canc s: %s, clOrdId: %d, chid: %d, reason: %d",
                  to_string(m->s), m->clOrdId, cancmsg.id, m->cxlRejReason);           
        }
        else
        {
          log_rej("OrderCancelReject response to canc s: %s, clOrdId: %d, chid: %d, reason: %d",
                  to_string(m->s), m->clOrdId, cancmsg.id, m->cxlRejReason);
        }
        cancmsg.sender->send(new frame::som::msg::CancReject(
            cancmsg.id, frame::som::msg::CancReject::FROMEXCHANGE, int(m->s)), this);
      }
      else if (m->cxlRejResponseTo == 2)
      {
        // response to order cancel replace request
        auto p = cloid_to_cancel_rep.find(m->clOrdId);
        if (p == cloid_to_cancel_rep.end())
        {
          log_omg("got cancel replace reject for unkown order ttoid: %s, clOrdId: %d",
                  m->TTOrdId, m->clOrdId);
          return;
        }
        const auto &cancreplmsg = p->second;
        log_omg("OrderCancelReject response to cancrepl s: %s, clOrdId: %d, chid: %d, chidtocan: %d, reason: %d",
                to_string(m->s), m->clOrdId, cancreplmsg.oid, cancreplmsg.oid_to_cancel, m->cxlRejReason);
        log_rej("OrderCancelReject response to cancrepl s: %s, clOrdId: %d, chid: %d, chidtocan: %d, reason: %d",
                to_string(m->s), m->clOrdId, cancreplmsg.oid, cancreplmsg.oid_to_cancel, m->cxlRejReason);
        cancreplmsg.sender->send(new frame::som::msg::Reject(cancreplmsg.oid_to_cancel), this);
        cancreplmsg.sender->send(new frame::som::msg::CancReject(
            cancreplmsg.oid, frame::som::msg::CancReject::REJECTONMOD), this);
      }
      else
        ERR("cant decode OrderCancelReject cxlRejResponseTo");
      // Keep latest reject per origClOrdId — emplace is a no-op on
      // duplicate key which would mask later state.
      cloid_to_last_reject.insert_or_assign(m->origClOrdId, *m);
    }

    void order_handler(const frame::som::msg::Order *m) noexcept
    {
      // Wrapper around the original body — sendNewOrderSingle /
      // sendCancelReplaceRequest throw on `noNews || !isLoggedOn` (i.e.
      // a session/SOM race where the SOM routes an order to us before
      // TT FIX has finished logging on + News-handshake).  Without this
      // try/catch the throw escapes a noexcept actor handler and the
      // framework terminates kaspr.  Instead, log the throw and reply
      // with a Reject so the SOM can decide what to do.
      try {
      if (m->oid_to_cancel >= 0)
      {
        auto p = choid_to_cloid.find(m->oid_to_cancel);
        ASSERT(p != choid_to_cloid.end(), "no such order");
        auto origClOid = p->second;
        log_omg("modifyOrder oid: %d, oid_to_cancel: %d, origCloid: %d",
                m->oid, m->oid_to_cancel, origClOid);
        auto a = frame::ref::RefData::get_asset(m->sym);
        ASSERT(a, "no such asset");
        // qty must be set such that qty remain is incresed or decreased to match m->sz
        auto p2 = cloid_to_last_execrep.find(origClOid);
        if (p2 == cloid_to_last_execrep.end())
        {
          // this order has not been acked yet?
          log_err("order_handler oid_to_cancel: %d, has no execution report", m->oid_to_cancel);
          m->sender->send(new frame::som::msg::Reject(m->oid), this);
          m->sender->send(new frame::som::msg::CancReject(m->oid, frame::som::msg::CancReject::REJECTONMOD), this);
          return;
        }
        int sz_delta = m->sz - p2->second.leavesQty;
        int qty = p2->second.qty + sz_delta;
        log_omg("setting qty: %d, request size: %d, working order qty: %d, working order leaves qty: %d",
                qty, m->sz, p2->second.qty, p2->second.leavesQty);
        ASSERT(qty >= 0, "qty miscalculated cannot not be negative");
        auto cloid = client->sendCancelReplaceRequest(
            origClOid,
            qty,
            m->px * a->get_units(),
            true);
        cloid_to_cancel_rep.emplace(std::make_pair(cloid, *m));
        choid_to_cloid[m->oid] = origClOid;
      }
      else
      {
        log_omg("sendOrder oid: %d", m->oid);
        auto a = frame::ref::RefData::get_asset(m->sym);
        ASSERT(a, "no such asset");
        auto cloid = client->sendNewOrderSingle(
            a->cfi_code.c_str(),
            a->name.c_str(),
            a->mnemonic.c_str(),
            m->px * a->get_units(),
            m->sz,
            m->side == en::bs::BUY ? 'B' : 'S',
            "LIM",
            "DAY");
        choid_to_cloid[m->oid] = cloid;
        cloid_to_order.emplace(std::make_pair(cloid, *m));
        log_omg("choid: %d = cloid: %d", m->oid, cloid);
      }
      } catch (const std::exception &e) {
        log_err("order_handler: TT FIX send threw: %s — rejecting oid=%d", e.what(), m->oid);
        if (m->sender)
          m->sender->send(new frame::som::msg::Reject(m->oid), this);
      }
    }

    void cancel_handler(const frame::som::msg::Cancel *m) noexcept
    {
      try {
        auto p = choid_to_cloid.find(m->id);
        ASSERT(p != choid_to_cloid.end(), "no such order");
        auto origClOid = p->second;
        log_omg("cancelOrder chid: %d, origClOid: %d", m->id, origClOid);
        auto cloid = client->sendCancelOrderRequest(origClOid);
        cloid_to_cancel.emplace(std::make_pair(cloid, *m));
      } catch (const std::exception &e) {
        log_err("cancel_handler: TT FIX send threw: %s — rejecting cancel id=%d", e.what(), m->id);
        if (m->sender)
          m->sender->send(new frame::som::msg::CancReject(
              m->id, frame::som::msg::CancReject::FROMEXCHANGE), this);
      }
    }

    void shutdown_handler(const actors::msg::Shutdown *) noexcept
    {
      if (initiator)
        initiator->stop();
    }
  };
}