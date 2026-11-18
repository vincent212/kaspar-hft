/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// One QuickFIX initiator session to an LMAX gateway: settings built in memory
// from SessionParams (the environment guard runs first), a file or memory
// store and a log that both redact the password, and a TLS or plain-TCP
// initiator. QuickFIX runs the session on its own thread; the Application's
// callbacks arrive there.

#include <memory>
#include <string>

#include "quickfix/Application.h"
#include "quickfix/Initiator.h"
#include "quickfix/Message.h"
#include "quickfix/SessionID.h"
#include "quickfix/SessionSettings.h"

#include "lmax_fix/LmaxFix.hpp"
#include "lmax_fix/RedactLog.hpp"

namespace lmax::fix
{
  class FixInitiator
  {
  public:
    // Throws std::runtime_error with the reason if the parameters are refused
    // (wrong host for the environment, production, missing CompIDs) or
    // QuickFIX rejects the settings.
    FixInitiator(const SessionParams &p, FIX::Application &app);
    ~FixInitiator();

    void start(); // connects; logon follows on QuickFIX's thread
    void stop();  // logout and join
    const FIX::SessionID &session_id() const { return sid_; }

    // Send on the session; false if QuickFIX did not send it (not logged on).
    bool send(FIX::Message m);
    // Drop the connection; QuickFIX reconnects after ReconnectInterval and
    // logs on again (with 141=Y when reset_on_logon). Session::logout would
    // disable the session and stop the reconnects.
    void reconnect();
    bool logged_on() const;

  private:
    FIX::SessionSettings settings_;
    std::unique_ptr<FIX::MessageStoreFactory> store_;
    std::unique_ptr<FIX::LogFactory> log_;
    std::unique_ptr<FIX::Initiator> init_;
    FIX::SessionID sid_;
    bool started_ = false;
  };
}
