/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// tt_logon: check a TT FIX order-routing session end to end, without kaspr.
//
//   tt_logon <cfg>
//       log on, wait for TT's News message (TT sends it when the session is
//       ready for orders), log out.
//
//   tt_logon <cfg> <cfi_code> <security_id> <symbol> <price> <qty> <B|S>
//       as above, then send one limit DAY order, wait for its acknowledgement,
//       cancel it, wait for the cancel, log out.
//
// Execution reports and rejects are printed as they arrive. The config is a
// QuickFIX session file, see ttom/config/tt_example.cfg.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "ttom/TTFixClient.hpp"
#include "ttom/CallBackIF.hpp"

#include "config.h"
#include "quickfix/FileStore.h"
#include "quickfix/SocketInitiator.h"
#include "quickfix/SSLSocketInitiator.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/Log.h"

namespace
{
  struct PrintCallbacks : public ttfixom::CallBackIF
  {
    std::atomic<bool> logged_on{false};
    std::atomic<bool> news{false};
    std::atomic<bool> acked{false};
    std::atomic<bool> done{false};    // order canceled, filled or rejected

    void onLogon() noexcept override
    {
      std::cout << "logged on" << std::endl;
      logged_on = true;
    }

    void onLogout() noexcept override
    {
      std::cout << "logged out" << std::endl;
      logged_on = false;
    }

    void onNews(const std::string &text) noexcept override
    {
      std::cout << "news: " << text << std::endl;
      news = true;
    }

    void onExecutionReport(
        ttfixom::OrdStatus s,
        ttfixom::ExecType x,
        uint64_t clOrdId,
        const std::string &TTOrdId,
        uint64_t cmeOrdId,
        uint qty,
        uint cumQty,
        uint leavesQty,
        bool has_px,
        double px,
        const std::string &,
        const std::string &,
        const std::string &,
        const std::string &text) noexcept override
    {
      std::cout << "execution report: exec_type=" << ttfixom::to_string(x)
                << " ord_status=" << ttfixom::to_string(s)
                << " clOrdId=" << clOrdId << " ttOrdId=" << TTOrdId
                << " cmeOrdId=" << cmeOrdId << " qty=" << qty
                << " cumQty=" << cumQty << " leavesQty=" << leavesQty;
      if (has_px)
        std::cout << " px=" << px;
      if (!text.empty())
        std::cout << " text=" << text;
      std::cout << std::endl;
      if (x == ttfixom::ExecType::NewOrder)
        acked = true;
      else if (x == ttfixom::ExecType::Canceled || x == ttfixom::ExecType::Rejected ||
               x == ttfixom::ExecType::Filled)
        done = true;
    }

    void onOrderCancelReject(
        ttfixom::OrdStatus s,
        uint64_t clOrdId,
        uint64_t origClOrdId,
        const std::string &ttOid,
        uint64_t,
        int cxlRejResponseTo,
        int cxlRejReason,
        const std::string &,
        const std::string &,
        const std::string &,
        const std::string &text) noexcept override
    {
      std::cout << "cancel reject: ord_status=" << ttfixom::to_string(s)
                << " clOrdId=" << clOrdId << " origClOrdId=" << origClOrdId
                << " ttOrdId=" << ttOid << " responseTo=" << cxlRejResponseTo
                << " reason=" << cxlRejReason << " text=" << text << std::endl;
      done = true;
    }
  };

  // Wait until flag is set, or give up after timeout_s seconds.
  bool wait_for(const std::atomic<bool> &flag, int timeout_s)
  {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    while (!flag && std::chrono::steady_clock::now() < end)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return flag;
  }
}

int main(int argc, char **argv)
{
  if (argc != 2 && argc != 8)
  {
    std::cerr << "usage: " << argv[0] << " <cfg>\n"
              << "       " << argv[0]
              << " <cfg> <cfi_code> <security_id> <symbol> <price> <qty> <B|S>\n";
    return 2;
  }

  try
  {
    FIX::SessionSettings settings(argv[1]);
    const auto &d = settings.get();
    std::string onBehalfOfSubID;
    if (d.has("OnBehalfOfSubID"))
      onBehalfOfSubID = d.getString("OnBehalfOfSubID");

    PrintCallbacks cb;
    ttfixom::TTFixClient client(
        &cb,
        d.getString("SecurityExchange"),
        d.getString("Account"),
        d.getString("Password"),
        d.getString("TargetCompID"),
        d.getString("SenderCompID"),
        onBehalfOfSubID);

    FIX::FileStoreFactory store(settings);
    FIX::ScreenLogFactory log(settings);
#ifdef USESSLFIX
    FIX::SSLSocketInitiator initiator(client, store, settings, log);
#else
    FIX::SocketInitiator initiator(client, store, settings, log);
#endif
    initiator.start();

    if (!wait_for(cb.logged_on, 30))
    {
      std::cerr << "no logon within 30 s" << std::endl;
      initiator.stop();
      return 1;
    }
    if (!wait_for(cb.news, 30))
    {
      std::cerr << "no News from TT within 30 s; the session is not ready for orders" << std::endl;
      initiator.stop();
      return 1;
    }

    int rc = 0;
    if (argc == 8)
    {
      const char side = argv[7][0];
      if (side != 'B' && side != 'S')
      {
        std::cerr << "side must be B or S" << std::endl;
        initiator.stop();
        return 2;
      }
      auto cloid = client.sendNewOrderSingle(
          argv[2], argv[3], argv[4], std::atof(argv[5]), std::atoi(argv[6]),
          side, "LIM", "DAY");
      std::cout << "sent order clOrdId=" << cloid << std::endl;

      if (wait_for(cb.acked, 15))
      {
        auto cxl = client.sendCancelOrderRequest(cloid);
        std::cout << "sent cancel clOrdId=" << cxl << std::endl;
        if (!wait_for(cb.done, 15))
        {
          std::cerr << "no cancel confirmation within 15 s" << std::endl;
          rc = 1;
        }
      }
      else if (!cb.done)
      {
        std::cerr << "no acknowledgement within 15 s" << std::endl;
        rc = 1;
      }
    }

    initiator.stop();
    return rc;
  }
  catch (const std::exception &e)
  {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }
}
