#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CbxSession: one FIX 5.0 SP2 session with Coinbase Exchange (order entry or
// drop copy), as the order manager and the drop-copy client see it.
//
// Implementations:
//   QfSession  the real one: QuickFIX SSLSocketInitiator (src/QfSession.cpp),
//              built only when QUICKFIX_PATH is set
//   SimCbx     an in-process Coinbase Exchange for --sim runs (SimCbx.hpp)
//   FakeCbx    the test double (tests/FakeCbx.hpp)
//
// Rules every implementation keeps (as OuchSession):
//   - events are delivered on the implementation's own thread (the test double
//     may deliver from inside a call), never after the destructor returned;
//     the receiver only queues them
//   - message() carries each application message as received
//   - nothing is queued across a disconnect: send() while not logged on
//     returns Disconnected

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "coinbase_exchange/Rest.hpp"
#include "coinbase_exchange_oe/FixMsg.hpp"

namespace coinbase_exchange::oe
{
  enum class CallStatus
  {
    Success,
    Busy,         // the venue's rate limit would be exceeded; wait for available()
    Disconnected, // not logged on; nothing was sent
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_on() = 0;
    virtual void logon_rejected(const std::string &reason) = 0; // Logout or Reject in answer to our Logon
    virtual void disconnected(const std::string &reason) = 0;
    virtual void busy() = 0;
    virtual void available() = 0;
    virtual void message(const fix::FixMsg &m) = 0;
  };

  class CbxSession
  {
  public:
    virtual ~CbxSession() = default;
    // Connect and log on; the answer comes as logged_on, logon_rejected or
    // disconnected. Called once per session object.
    virtual void logon() = 0;
    // An application message (35=D, F, H, ...). The session adds the header.
    virtual CallStatus send(const fix::FixMsg &m) = 0;
    virtual void logout() = 0;
  };

  // Where and how to log on (order-entry-messages5, Logon 35=A; drop-copy).
  struct SessionConfig
  {
    Environment environment = Environment::Sandbox;
    std::string host;              // empty: from the environment
    int port = 0;                  // 0: from the environment
    bool drop_copy = false;        // DropCopyFlag 9406=Y and the drop-copy host
    char cancel_on_disconnect = 'Y'; // 8013: 'Y' profile, 'S' session, 0 none
    int heartbeat_s = 10;          // HeartBtInt, at most 30 (drop copy: 300)
    std::string key_file;          // credentials (coinbase_exchange/Rest.hpp)
    std::string store_dir;         // QuickFIX FileStorePath and FileLogPath
    std::string ca_file;           // TLS: CA bundle or the sandbox's certificate
    std::string transport_dict, app_dict; // Coinbase's FIXT11 and FIX50 dictionaries
    int reconnect_s = 5;
  };

  using SessionFactory = std::function<std::unique_ptr<CbxSession>(const SessionConfig &, SessionEvents &)>;

  // Coinbase's Logon fields, signed (order-entry-messages5: the HMAC-SHA256 of
  // SendingTime, MsgType, MsgSeqNum, SenderCompID, TargetCompID and Passphrase
  // joined by SOH, base64, in RawData). `sending_time` must have exactly three
  // fraction digits. Returns false if the secret is not base64.
  bool sign_logon(fix::FixMsg &logon, const std::string &sending_time, const std::string &seq_num,
                  const std::string &key, const std::string &secret, const std::string &passphrase,
                  char cancel_on_disconnect, bool drop_copy);

  // A raw FIX message with Password (554) and RawData (96) replaced by "***",
  // for logs (QfSession's message log uses it).
  std::string mask_secrets(const std::string &raw);

  // The real session over QuickFIX (nullptr factory when built without it).
  SessionFactory quickfix_session_factory();
}
