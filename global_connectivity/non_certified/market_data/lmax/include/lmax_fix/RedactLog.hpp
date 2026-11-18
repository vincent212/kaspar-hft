/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// QuickFIX's FileLog writes the Logon as sent, and its FileStore persists it
// for resends: both would put the LMAX password (554) on disk. RedactLog and
// RedactStore replace its value with "***" before anything is written. Admin
// messages are never resent (QuickFIX gap-fills them), so the stored Logon is
// not needed verbatim.

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "quickfix/Log.h"
#include "quickfix/MessageStore.h"

namespace lmax::fix
{
  // Replaces the value of every 554= field in a FIX string with ***.
  std::string redact(const std::string &fix);

  class RedactLog : public FIX::Log
  {
  public:
    explicit RedactLog(const std::string &path);
    void clear() override {}
    void backup() override {}
    void onIncoming(const std::string &s) override { write("IN  ", s); }
    void onOutgoing(const std::string &s) override { write("OUT ", s); }
    void onEvent(const std::string &s) override { write("EVT ", s); }

  private:
    void write(const char *what, const std::string &s);
    std::mutex mu_;
    std::ofstream out_;
  };

  // Wraps another store (FileStore, MemoryStore) and redacts what it saves.
  class RedactStore : public FIX::MessageStore
  {
  public:
    explicit RedactStore(FIX::MessageStore *inner, FIX::MessageStoreFactory &f) : in_(inner), f_(f) {}
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
    explicit RedactStoreFactory(std::unique_ptr<FIX::MessageStoreFactory> inner) : in_(std::move(inner)) {}
    FIX::MessageStore *create(const FIX::UtcTimeStamp &now, const FIX::SessionID &id) override
    {
      return new RedactStore(in_->create(now, id), *in_);
    }
    void destroy(FIX::MessageStore *s) override { delete s; }

  private:
    std::unique_ptr<FIX::MessageStoreFactory> in_;
  };

  class RedactLogFactory : public FIX::LogFactory
  {
  public:
    explicit RedactLogFactory(std::string dir) : dir_(std::move(dir)) {}
    FIX::Log *create() override;
    FIX::Log *create(const FIX::SessionID &id) override;
    void destroy(FIX::Log *log) override { delete log; }

  private:
    std::string dir_;
  };
}
