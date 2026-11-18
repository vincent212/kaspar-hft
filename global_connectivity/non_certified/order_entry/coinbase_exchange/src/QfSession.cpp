/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// QfSession: CbxSession over QuickFIX (FIXT 1.1 / FIX 5.0 SP2) with TLS
// (SSLSocketInitiator). One session object is one connection: QuickFIX's own
// reconnect is pushed out of the way (ReconnectInterval one day) and the
// owner (CbxOM, the drop-copy client) makes a new session after a disconnect,
// as for the other order managers.
//
// Logon (order-entry-messages5): toAdmin signs the outgoing Logon with the
// API secret over SendingTime, MsgType, MsgSeqNum, SenderCompID, TargetCompID
// and Passphrase, and adds Username, Password, RawDataLength, RawData,
// DefaultApplVerID 9, CancelOrdersOnDisconnect (order entry) or DropCopyFlag
// (drop copy). SendingTime has exactly three fraction digits
// (TimestampPrecision 3), as the signature requires. ResetOnLogon: Coinbase
// resets the client's numbering after every disconnect anyway.
//
// Credentials are read from the key file when the session is made, and never
// logged: the message log is MaskingLog, which writes every message as
// QuickFIX's file log would but with Password (554) and RawData (96)
// replaced by "***".

#include "coinbase_exchange_oe/CbxSession.hpp"

#ifdef CBX_WITH_QUICKFIX

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

#include "coinbase_exchange/Num.hpp"
#include "quickfix/Application.h"
#include "quickfix/FileLog.h"
#include "quickfix/FileStore.h"
#include "quickfix/Message.h"
#include "quickfix/NullStore.h"
#include "quickfix/SSLSocketInitiator.h"
#include "quickfix/Session.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/SocketInitiator.h"

namespace coinbase_exchange::oe
{
namespace
{
  // QuickFIX's message and event log, with the Logon's secrets masked.
  class MaskingLog : public FIX::Log
  {
  public:
    MaskingLog(const std::string &dir, const std::string &name)
    {
      std::system(("mkdir -p '" + dir + "'").c_str());
      msgs_.open(dir + "/" + name + ".messages.log", std::ios::app);
      events_.open(dir + "/" + name + ".event.log", std::ios::app);
    }
    void clear() override {}
    void backup() override {}
    void onIncoming(const std::string &v) override { line(msgs_, "in ", v); }
    void onOutgoing(const std::string &v) override { line(msgs_, "out", v); }
    void onEvent(const std::string &v) override { line(events_, "", v); }

  private:
    std::mutex mu_;
    std::ofstream msgs_, events_;
    void line(std::ofstream &f, const char *dir, const std::string &v)
    {
      std::lock_guard<std::mutex> l(mu_);
      f << fix_time_ms(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count()))
        << " " << dir << " " << mask_secrets("\x01" + v).substr(1) << '\n';
      f.flush();
    }
  };
  class MaskingLogFactory : public FIX::LogFactory
  {
  public:
    explicit MaskingLogFactory(std::string dir) : dir_(std::move(dir)) {}
    FIX::Log *create() override { return new MaskingLog(dir_, "global"); }
    FIX::Log *create(const FIX::SessionID &s) override
    {
      return new MaskingLog(dir_, s.getSenderCompID().getValue().substr(0, 8) + "-" + s.getTargetCompID().getValue());
    }
    void destroy(FIX::Log *l) override { delete l; }

  private:
    std::string dir_;
  };

  class QfSession : public CbxSession, public FIX::Application
  {
  public:
    QfSession(const SessionConfig &cfg, SessionEvents &ev) : cfg_(cfg), ev_(ev)
    {
      std::string err;
      if (!load_credentials(cfg.key_file, creds_, err))
        throw std::runtime_error(err);
      auto ep = endpoints(cfg.environment);
      host_ = !cfg.host.empty() ? cfg.host : cfg.drop_copy ? ep.fix_dc_host : ep.fix_ord_host;
      port_ = cfg.port ? cfg.port : cfg.drop_copy ? ep.fix_dc_port : ep.fix_ord_port;
      // a plain-TCP loopback, for the tests' acceptor only
      plain_ = host_ == "127.0.0.1";
      if (!plain_ && !host_allowed(cfg.environment, host_, &creds_, err))
        throw std::runtime_error(err);
      key_ = cfg.drop_copy && !creds_.dc_key.empty() ? creds_.dc_key
             : !cfg.drop_copy && !creds_.oe_key.empty() ? creds_.oe_key
                                                        : creds_.key;
      secret_ = cfg.drop_copy && !creds_.dc_secret.empty() ? creds_.dc_secret
                : !cfg.drop_copy && !creds_.oe_secret.empty() ? creds_.oe_secret
                                                              : creds_.secret;
      passphrase_ = cfg.drop_copy && !creds_.dc_passphrase.empty() ? creds_.dc_passphrase
                    : !cfg.drop_copy && !creds_.oe_passphrase.empty() ? creds_.oe_passphrase
                                                                      : creds_.passphrase;
      id_ = FIX::SessionID("FIXT.1.1", key_, "Coinbase");
      FIX::Dictionary d;
      d.setString("ConnectionType", "initiator");
      d.setString("BeginString", "FIXT.1.1");
      d.setString("DefaultApplVerID", "FIX.5.0SP2");
      d.setString("SenderCompID", key_);
      d.setString("TargetCompID", "Coinbase");
      d.setString("SocketConnectHost", host_);
      d.setInt("SocketConnectPort", port_);
      d.setInt("HeartBtInt", cfg.heartbeat_s);
      d.setString("StartTime", "00:00:00");
      d.setString("EndTime", "00:00:00");
      d.setInt("ReconnectInterval", 86400);
      d.setString("ResetOnLogon", "Y");
      d.setInt("TimestampPrecision", 3);
      d.setString("UseDataDictionary", cfg.transport_dict.empty() ? "N" : "Y");
      if (!cfg.transport_dict.empty())
      {
        d.setString("TransportDataDictionary", cfg.transport_dict);
        d.setString("AppDataDictionary", cfg.app_dict);
        // Coinbase may send fields its 2024 dictionary does not list (the
        // header's RptSeq, fields added since): accepted, not rejected.
        d.setString("ValidateUserDefinedFields", "N");
        d.setString("AllowUnknownMsgFields", "Y");
        d.setString("ValidateFieldsOutOfOrder", "N");
      }
      std::string store = cfg.store_dir.empty() ? "/tmp/kaspar_cbx_fix" : cfg.store_dir;
      d.setString("FileStorePath", store + "/store");
      d.setString("FileLogPath", store + "/log");
      if (!plain_)
      {
        d.setString("SSLProtocol", "all -SSLv2 -SSLv3 -TLSv1 -TLSv1_1");
        if (!cfg.ca_file.empty())
          d.setString("CertificationAuthoritiesFile", cfg.ca_file);
        d.setString("CertificateVerifyLevel", "1");
      }
      settings_.set(id_, d);
      store_ = std::make_unique<FIX::FileStoreFactory>(settings_);
      log_ = std::make_unique<MaskingLogFactory>(store + "/log");
    }

    ~QfSession() override
    {
      if (init_)
        init_->stop(true);
    }

    void logon() override
    {
      try
      {
        if (plain_)
          init_ = std::make_unique<FIX::SocketInitiator>(*this, *store_, settings_, *log_);
        else
          init_ = std::make_unique<FIX::SSLSocketInitiator>(*this, *store_, settings_, *log_);
        init_->start();
      }
      catch (const std::exception &e)
      {
        ev_.disconnected(std::string("QuickFIX: ") + e.what());
      }
    }

    CallStatus send(const fix::FixMsg &m) override
    {
      if (!logged_on_)
        return CallStatus::Disconnected;
      FIX::Message q;
      q.getHeader().setField(FIX::FIELD::MsgType, m.type());
      for (const auto &[tag, v] : m.fields)
        if (tag != fix::MsgType)
          q.setField(tag, v);
      try
      {
        return FIX::Session::sendToTarget(q, id_) ? CallStatus::Success : CallStatus::Disconnected;
      }
      catch (const FIX::SessionNotFound &)
      {
        return CallStatus::Disconnected;
      }
    }

    void logout() override
    {
      if (auto s = FIX::Session::lookupSession(id_))
        s->logout("logout");
    }

    // ---- FIX::Application (QuickFIX's thread) ---------------------------------------------
    void onCreate(const FIX::SessionID &) override {}
    void onLogon(const FIX::SessionID &) override
    {
      logged_on_ = true;
      ev_.logged_on();
    }
    void onLogout(const FIX::SessionID &) override
    {
      bool was = logged_on_.exchange(false);
      if (was)
        ev_.disconnected("logged out");
      else if (!logon_answered_.exchange(true))
        ev_.disconnected("connection closed before logon");
    }
    void toAdmin(FIX::Message &msg, const FIX::SessionID &) override
    {
      const auto &h = msg.getHeader();
      if (h.getField(FIX::FIELD::MsgType) != "A")
        return;
      fix::FixMsg l("A");
      if (!sign_logon(l, h.getField(FIX::FIELD::SendingTime), h.getField(FIX::FIELD::MsgSeqNum), key_, secret_,
                      passphrase_, cfg_.cancel_on_disconnect, cfg_.drop_copy))
        return; // cannot happen: the key file was checked
      for (const auto &[tag, v] : l.fields)
        if (tag != fix::MsgType)
          msg.setField(tag, v);
      msg.setField(fix::ResetSeqNumFlag, "Y");
    }
    void fromAdmin(const FIX::Message &msg, const FIX::SessionID &) override
    {
      const auto t = msg.getHeader().getField(FIX::FIELD::MsgType);
      std::string text = msg.isSetField(fix::Text) ? msg.getField(fix::Text) : std::string();
      if (t == "5" && !logged_on_ && !logon_answered_.exchange(true))
        ev_.logon_rejected("Logout: " + text);
      else if (t == "3")
      {
        auto f = fix::FixMsg::parse(msg.toString());
        if (f)
          ev_.message(*f);
      }
    }
    void toApp(FIX::Message &, const FIX::SessionID &) override {}
    void fromApp(const FIX::Message &msg, const FIX::SessionID &) override
    {
      auto f = fix::FixMsg::parse(msg.toString());
      if (f)
        ev_.message(*f);
      else
        std::cerr << "QfSession: message did not parse" << std::endl;
    }

  private:
    SessionConfig cfg_;
    SessionEvents &ev_;
    Credentials creds_;
    std::string host_, key_, secret_, passphrase_;
    int port_ = 0;
    bool plain_ = false;
    FIX::SessionID id_;
    FIX::SessionSettings settings_;
    std::unique_ptr<FIX::MessageStoreFactory> store_;
    std::unique_ptr<FIX::LogFactory> log_;
    std::unique_ptr<FIX::Initiator> init_;
    std::atomic<bool> logged_on_{false}, logon_answered_{false};
  };
}

SessionFactory quickfix_session_factory()
{
  return [](const SessionConfig &cfg, SessionEvents &ev) -> std::unique_ptr<CbxSession> {
    return std::make_unique<QfSession>(cfg, ev);
  };
}
}

#else

namespace coinbase_exchange::oe
{
SessionFactory quickfix_session_factory()
{
  return nullptr;
}
}

#endif
