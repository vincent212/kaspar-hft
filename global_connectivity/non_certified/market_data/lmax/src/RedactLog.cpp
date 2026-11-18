/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_fix/RedactLog.hpp"

#include <filesystem>

#include "lmax_fix/LmaxFix.hpp"
#include "quickfix/SessionID.h"

namespace lmax::fix
{
  std::string redact(const std::string &s)
  {
    std::string out = s;
    size_t pos = 0;
    while ((pos = out.find("554=", pos)) != std::string::npos)
    {
      if (pos != 0 && out[pos - 1] != '\x01' && out[pos - 1] != '|')
      {
        pos += 4;
        continue;
      }
      size_t end = out.find_first_of("\x01|", pos + 4);
      if (end == std::string::npos)
        end = out.size();
      out.replace(pos + 4, end - pos - 4, "***");
      pos += 7;
    }
    return out;
  }

  RedactLog::RedactLog(const std::string &path) : out_(path, std::ios::app) {}

  void RedactLog::write(const char *what, const std::string &s)
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!out_)
      return;
    const uint64_t ns = now_ns();
    out_ << ns / 1000000000ULL << '.' << (ns / 1000) % 1000000 << ' ' << what << redact(s) << '\n';
    out_.flush();
  }

  FIX::Log *RedactLogFactory::create() { return new RedactLog(dir_ + "/global.log"); }

  FIX::Log *RedactLogFactory::create(const FIX::SessionID &id)
  {
    std::filesystem::create_directories(dir_);
    std::string name = id.getSenderCompID().getValue() + "-" + id.getTargetCompID().getValue() + ".log";
    return new RedactLog(dir_ + "/" + name);
  }
}
