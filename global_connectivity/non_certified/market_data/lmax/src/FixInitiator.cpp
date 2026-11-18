/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_fix/FixInitiator.hpp"

#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "quickfix/FileStore.h"
#include "quickfix/Session.h"
#include "quickfix/SocketInitiator.h"
#ifdef HAVE_SSL
#include "quickfix/SSLSocketInitiator.h"
#endif

namespace lmax::fix
{
  FixInitiator::FixInitiator(const SessionParams &p, FIX::Application &app)
  {
    std::string text, err;
    if (!settings_text(p, text, err))
      throw std::runtime_error(err);
    std::istringstream in(text);
    settings_ = FIX::SessionSettings(in);
    auto ids = settings_.getSessions();
    if (ids.size() != 1)
      throw std::runtime_error("expected one session in the settings");
    sid_ = *ids.begin();

    std::unique_ptr<FIX::MessageStoreFactory> inner;
    if (!p.store_path.empty())
    {
      std::filesystem::create_directories(p.store_path);
      inner = std::make_unique<FIX::FileStoreFactory>(p.store_path);
    }
    else
      inner = std::make_unique<FIX::MemoryStoreFactory>();
    store_ = std::make_unique<RedactStoreFactory>(std::move(inner));
    if (!p.log_path.empty())
      log_ = std::make_unique<RedactLogFactory>(p.log_path);
    else
      log_ = std::make_unique<FIX::ScreenLogFactory>(false, false, false);

    if (p.tls)
    {
#ifdef HAVE_SSL
      init_ = std::make_unique<FIX::SSLSocketInitiator>(app, *store_, settings_, *log_);
#else
      throw std::runtime_error("TLS asked for, but QuickFIX was built without HAVE_SSL");
#endif
    }
    else
      init_ = std::make_unique<FIX::SocketInitiator>(app, *store_, settings_, *log_);
  }

  FixInitiator::~FixInitiator() { stop(); }

  void FixInitiator::start()
  {
    if (started_)
      return;
    init_->start();
    started_ = true;
  }

  void FixInitiator::stop()
  {
    if (!started_)
      return;
    started_ = false;
    try
    {
      init_->stop();
    }
    catch (...)
    {
    }
  }

  bool FixInitiator::send(FIX::Message m)
  {
    try
    {
      return FIX::Session::sendToTarget(m, sid_);
    }
    catch (const std::exception &)
    {
      return false;
    }
  }

  void FixInitiator::reconnect()
  {
    if (auto *s = FIX::Session::lookupSession(sid_))
      s->disconnect();
  }

  bool FixInitiator::logged_on() const
  {
    auto *s = FIX::Session::lookupSession(sid_);
    return s && s->isLoggedOn();
  }
}
