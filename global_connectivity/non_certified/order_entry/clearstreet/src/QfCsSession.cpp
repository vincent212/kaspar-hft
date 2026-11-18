/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// QfCsSession: CsSession over a QuickFIX initiator. NOT CERTIFIED: compiled
// and run against a local QuickFIX acceptor in tests/test_qf_session.cpp,
// never against Clear Street.

#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "quickfix/Application.h"
#include "quickfix/FileStore.h"
#include "quickfix/Log.h"
#include "quickfix/Session.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/SocketInitiator.h"
#if HAVE_SSL
#include "quickfix/SSLSocketInitiator.h"
#endif

#include "clearstreet_om/CsSession.hpp"

namespace clearstreet::om
{
  std::string redact(const std::string &fix)
  {
    std::string out = fix;
    size_t pos = 0;
    while ((pos = out.find("554=", pos)) != std::string::npos)
    {
      // only a tag at the start of a field: start of string or after SOH / '|'
      if (pos != 0 && out[pos - 1] != '\x01' && out[pos - 1] != '|')
      {
        pos += 4;
        continue;
      }
      size_t v = pos + 4, end = out.find_first_of("\x01|", v);
      if (end == std::string::npos)
        end = out.size();
      out.replace(v, end - v, "***");
      pos = v + 3;
    }
    return out;
  }

  namespace
  {
    // QuickFIX's FileLog writes the Logon as sent and its FileStore keeps it
    // for resends: both would put the API key (554) on disk. These wrappers
    // redact it first. Admin messages are not resent (QuickFIX gap-fills
    // them), so the stored Logon is never needed verbatim.
    class RedactLog : public FIX::Log
    {
    public:
      explicit RedactLog(const std::string &path)
      {
        if (!path.empty())
          out_.open(path, std::ios::app);
      }
      void clear() override {}
      void backup() override {}
      void onIncoming(const std::string &s) override { write("IN  ", s); }
      void onOutgoing(const std::string &s) override { write("OUT ", s); }
      void onEvent(const std::string &s) override { write("EVT ", s); }

    private:
      void write(const char *what, const std::string &s)
      {
        std::lock_guard<std::mutex> l(mu_);
        if (out_)
          out_ << FIX::UtcTimeStampConvertor::convert(FIX::UtcTimeStamp(), 3) << ' ' << what << redact(s) << '\n'
               << std::flush;
      }
      std::mutex mu_;
      std::ofstream out_;
    };

    class RedactLogFactory : public FIX::LogFactory
    {
    public:
      explicit RedactLogFactory(std::string dir) : dir_(std::move(dir)) {}
      FIX::Log *create() override { return new RedactLog(file("events")); }
      FIX::Log *create(const FIX::SessionID &id) override
      {
        return new RedactLog(file(id.getSenderCompID().getString() + "-" + id.getTargetCompID().getString()));
      }
      void destroy(FIX::Log *l) override { delete l; }

    private:
      std::string file(const std::string &base)
      {
        if (dir_.empty())
          return {};
        std::filesystem::create_directories(dir_);
        return dir_ + "/" + base + ".log";
      }
      std::string dir_;
    };

    class RedactStore : public FIX::MessageStore
    {
    public:
      RedactStore(FIX::MessageStore *in, FIX::MessageStoreFactory &f) : in_(in), f_(f) {}
      ~RedactStore() override { f_.destroy(in_); }
      bool set(int n, const std::string &s) override { return in_->set(n, redact(s)); }
      void get(int b, int e, std::vector<std::string> &v) const override { in_->get(b, e, v); }
      int getNextSenderMsgSeqNum() const override { return in_->getNextSenderMsgSeqNum(); }
      int getNextTargetMsgSeqNum() const override { return in_->getNextTargetMsgSeqNum(); }
      void setNextSenderMsgSeqNum(int n) override { in_->setNextSenderMsgSeqNum(n); }
      void setNextTargetMsgSeqNum(int n) override { in_->setNextTargetMsgSeqNum(n); }
      void incrNextSenderMsgSeqNum() override { in_->incrNextSenderMsgSeqNum(); }
      void incrNextTargetMsgSeqNum() override { in_->incrNextTargetMsgSeqNum(); }
      FIX::UtcTimeStamp getCreationTime() const override { return in_->getCreationTime(); }
      void reset(const FIX::UtcTimeStamp &now) override { in_->reset(now); }
      void refresh() override { in_->refresh(); }

    private:
      FIX::MessageStore *in_;
      FIX::MessageStoreFactory &f_;
    };

    class RedactStoreFactory : public FIX::MessageStoreFactory
    {
    public:
      explicit RedactStoreFactory(const std::string &path) : in_(path) {}
      FIX::MessageStore *create(const FIX::UtcTimeStamp &now, const FIX::SessionID &id) override
      {
        return new RedactStore(in_.create(now, id), in_);
      }
      void destroy(FIX::MessageStore *s) override { delete s; }

    private:
      FIX::FileStoreFactory in_;
    };

    class QfCsSession : public CsSession, public FIX::Application
    {
    public:
      QfCsSession(const QfSessionConfig &cfg, SessionEvents &ev)
          : cfg_(cfg), ev_(ev), id_("FIX.4.2", cfg.sender_comp_id, cfg.target_comp_id), store_(cfg.store_path),
            logs_(cfg.log_path)
      {
        if (cfg_.host.empty() || cfg_.port <= 0 || cfg_.sender_comp_id.empty() || cfg_.target_comp_id.empty())
          throw std::runtime_error("host, port, sender_comp_id and target_comp_id are required");
        if (cfg_.api_key.empty())
          throw std::runtime_error("no API key");
        if (cfg_.store_path.empty())
          throw std::runtime_error("store_path is required (sequence numbers persist across reconnects)");
        FIX::Dictionary d;
        d.setString("ConnectionType", "initiator");
        d.setString("SocketConnectHost", cfg_.host);
        d.setInt("SocketConnectPort", cfg_.port);
        d.setInt("HeartBtInt", cfg_.heartbeat);
        d.setInt("ReconnectInterval", cfg_.reconnect_interval);
        if (!cfg_.extra.count("NonStopSession"))
        {
          d.setString("StartDay", cfg_.start_day);
          d.setString("StartTime", cfg_.start_time);
          d.setString("EndDay", cfg_.end_day);
          d.setString("EndTime", cfg_.end_time);
        }
        d.setString("FileStorePath", cfg_.store_path);
        // Clear Street journals sequence state across disconnects and resets
        // only on a coordinated 141=Y (guide, "Sequence numbers and recovery").
        d.setString("ResetOnLogon", "N");
        d.setString("ResetOnLogout", "N");
        d.setString("ResetOnDisconnect", "N");
        d.setString("PersistMessages", "Y");
        d.setString("SocketNodelay", "Y");
        d.setString("TimestampPrecision", "3");
        if (!cfg_.dictionary.empty())
        {
          d.setString("UseDataDictionary", "Y");
          d.setString("DataDictionary", cfg_.dictionary);
          // feed.md section 3: an inbound report that fails validation would
          // be answered with a Reject and lost, so only framing is strict.
          d.setString("ValidateFieldsOutOfOrder", "N");
          d.setString("ValidateUserDefinedFields", "N");
          d.setString("AllowUnknownMsgFields", "Y");
        }
        else
          d.setString("UseDataDictionary", "N");
        for (auto &[k, v] : cfg_.extra)
          d.setString(k, v);
        settings_.set(id_, d);
#if HAVE_SSL
        if (cfg_.tls)
          init_ = std::make_unique<FIX::SSLSocketInitiator>(*this, store_, settings_, logs_);
        else
#endif
        {
          if (cfg_.tls)
            throw std::runtime_error("tls = yes but QuickFIX was built without SSL");
          init_ = std::make_unique<FIX::SocketInitiator>(*this, store_, settings_, logs_);
        }
      }
      ~QfCsSession() override { stop(); }

      void start() override { init_->start(); }
      void stop() override
      {
        if (init_ && !stopped_.exchange(true))
          init_->stop(); // logout, then joins QuickFIX's threads
      }

      CallStatus send_new(const NewOrderReq &r) override { return send(build_new_order(r)); }
      CallStatus send_cancel(const CancelReq &r) override { return send(build_cancel(r)); }
      CallStatus send_replace(const ReplaceReq &r) override { return send(build_replace(r)); }

      // ---- FIX::Application, on QuickFIX's thread -------------------------
      void onCreate(const FIX::SessionID &) override {}
      void onLogon(const FIX::SessionID &) override
      {
        logged_on_ = true;
        std::string text;
        {
          std::lock_guard<std::mutex> l(mu_);
          text.swap(logon_text_);
        }
        ev_.logged_on(text);
      }
      void onLogout(const FIX::SessionID &) override
      {
        const bool was = logged_on_.exchange(false);
        std::string text;
        {
          std::lock_guard<std::mutex> l(mu_);
          text.swap(logout_text_);
        }
        if (was || !text.empty())
          ev_.logged_out(text);
      }
      void toAdmin(FIX::Message &m, const FIX::SessionID &) override
      {
        try
        {
          if (m.getHeader().getField(35) == "A")
          {
            m.setField(553, cfg_.sender_comp_id); // Username: the SenderCompID
            m.setField(554, cfg_.api_key);        // Password: the API key
          }
        }
        catch (...)
        {
        }
      }
      void toApp(FIX::Message &m, const FIX::SessionID &) override
      {
        // Remember which request went under which MsgSeqNum, so a session
        // Reject (45=RefSeqNum) can be matched to its order.
        try
        {
          uint64_t seq = std::strtoull(m.getHeader().getField(34).c_str(), nullptr, 10);
          std::string type = m.getHeader().getField(35);
          std::string cid = m.isSetField(11) ? m.getField(11) : std::string();
          std::lock_guard<std::mutex> l(mu_);
          if (sent_.emplace(seq, std::make_pair(type, cid)).second)
            order_.push_back(seq);
          while (order_.size() > 200000)
          {
            sent_.erase(order_.front());
            order_.pop_front();
          }
        }
        catch (...)
        {
        }
      }
      void fromAdmin(const FIX::Message &m, const FIX::SessionID &) override
      {
        try
        {
          const std::string &t = m.getHeader().getField(35);
          std::lock_guard<std::mutex> l(mu_);
          if (t == "A")
            logon_text_ = m.isSetField(58) ? m.getField(58) : std::string();
          else if (t == "5")
            logout_text_ = m.isSetField(58) ? m.getField(58) : std::string();
        }
        catch (...)
        {
        }
        try
        {
          if (m.getHeader().getField(35) == "3")
          {
            SessionReject r;
            std::string err;
            if (!parse_session_reject(m, r, err))
              return ev_.unparsed("Reject: " + err + ": " + m.toString());
            {
              std::lock_guard<std::mutex> l(mu_);
              auto it = sent_.find(r.ref_seq);
              if (it != sent_.end())
              {
                if (r.ref_msg_type.empty())
                  r.ref_msg_type = it->second.first;
                if (r.ref_msg_type == it->second.first)
                  r.clordid = it->second.second;
              }
            }
            ev_.session_reject(r);
          }
        }
        catch (const std::exception &e)
        {
          ev_.unparsed(std::string("Reject: ") + e.what());
        }
      }
      // Never throws: an exception would make QuickFIX reject the report.
      void fromApp(const FIX::Message &m, const FIX::SessionID &) override
      {
        std::string err;
        try
        {
          const std::string &t = m.getHeader().getField(35);
          if (t == "8")
          {
            ExecReport r;
            if (!parse_exec(m, r, err))
              return ev_.unparsed("ExecutionReport: " + err + ": " + m.toString());
            r.raw = m.toString();
            ev_.exec(r);
          }
          else if (t == "9")
          {
            CancelReject r;
            if (!parse_cancel_reject(m, r, err))
              return ev_.unparsed("OrderCancelReject: " + err + ": " + m.toString());
            ev_.cancel_reject(r);
          }
          else
            ev_.unparsed("unexpected MsgType " + t + ": " + m.toString());
        }
        catch (const std::exception &e)
        {
          ev_.unparsed(e.what());
        }
      }

    private:
      CallStatus send(FIX::Message m)
      {
        if (!logged_on_)
          return CallStatus::Disconnected;
        try
        {
          return FIX::Session::sendToTarget(m, id_) ? CallStatus::Success : CallStatus::Disconnected;
        }
        catch (...)
        {
          return CallStatus::Disconnected;
        }
      }

      QfSessionConfig cfg_;
      SessionEvents &ev_;
      FIX::SessionID id_;
      FIX::SessionSettings settings_;
      RedactStoreFactory store_;
      RedactLogFactory logs_;
      std::unique_ptr<FIX::Initiator> init_;
      std::atomic<bool> logged_on_{false};
      std::atomic<bool> stopped_{false};
      std::mutex mu_;
      std::string logon_text_, logout_text_;
      std::unordered_map<uint64_t, std::pair<std::string, std::string>> sent_; // MsgSeqNum -> (MsgType, ClOrdID)
      std::deque<uint64_t> order_;
    };
  }

  SessionFactory qf_session_factory(QfSessionConfig cfg)
  {
    return [cfg](SessionEvents &ev) -> std::unique_ptr<CsSession> { return std::make_unique<QfCsSession>(cfg, ev); };
  }
}
