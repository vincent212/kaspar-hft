/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_fix/LmaxFix.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace lmax::fix
{
  const char *to_string(Env e)
  {
    switch (e)
    {
    case Env::Loopback: return "loopback";
    case Env::Demo: return "demo";
    case Env::Uat: return "uat";
    case Env::Production: return "production";
    }
    return "?";
  }

  bool parse_env(const std::string &s, Env &out)
  {
    if (s == "loopback") out = Env::Loopback;
    else if (s == "demo") out = Env::Demo;
    else if (s == "uat") out = Env::Uat;
    else if (s == "production" || s == "prod") out = Env::Production;
    else return false;
    return true;
  }

  namespace
  {
    std::string lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
      return s;
    }
    bool ends_with(const std::string &s, const std::string &suffix)
    {
      return s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    bool numeric_host(const std::string &h)
    {
      return !h.empty() && std::all_of(h.begin(), h.end(), [](char c) { return std::isdigit((unsigned char)c) || c == '.' || c == ':'; });
    }
  }

  bool host_allowed(Env env, const std::string &host_in, std::string &why)
  {
    const std::string host = lower(host_in);
    switch (env)
    {
    case Env::Production:
      why = "production is refused: the LMAX handlers are not certified";
      return false;
    case Env::Loopback:
      if (host == "127.0.0.1" || host == "localhost")
        return true;
      why = "loopback environment: only 127.0.0.1 or localhost, not '" + host_in + "'";
      return false;
    case Env::Demo:
      if (!numeric_host(host) && ends_with(host, ".london-demo.lmax.com"))
        return true;
      why = "demo environment: host must be *.london-demo.lmax.com, not '" + host_in + "'";
      return false;
    case Env::Uat:
      if (!numeric_host(host) && ends_with(host, ".london-uat.lmax.com"))
        return true;
      why = "uat environment: host must be *.london-uat.lmax.com, not '" + host_in + "'";
      return false;
    }
    why = "unknown environment";
    return false;
  }

  bool load_credentials(const std::string &path, Credentials &c, std::string &err)
  {
    std::ifstream in(path);
    if (!in)
      return err = "cannot open key file " + path, false;
    nlohmann::json j;
    try
    {
      in >> j;
    }
    catch (const std::exception &e)
    {
      return err = "key file " + path + " is not JSON: " + e.what(), false;
    }
    auto str = [&](const char *k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
    const std::string env = str("environment");
    if (!parse_env(env, c.env))
      return err = "key file: environment must be demo or uat, not '" + env + "'", false;
    if (c.env == Env::Production)
      return err = "key file: production is refused", false;
    c.username = str("username");
    c.password = str("password");
    if (c.username.empty() || c.password.empty())
      return err = "key file: username and password are required", false;
    c.md_sender = str("md_sender_comp_id");
    c.md_target = str("md_target_comp_id");
    c.trade_sender = str("trade_sender_comp_id");
    c.trade_target = str("trade_target_comp_id");
    if (c.md_sender.empty())
      c.md_sender = c.username;
    if (c.trade_sender.empty())
      c.trade_sender = c.username;
    c.account = str("account");
    return true;
  }

  bool settings_text(const SessionParams &p, std::string &out, std::string &err)
  {
    if (!host_allowed(p.env, p.host, err))
      return false;
    if (p.sender.empty() || p.target.empty())
      return err = "SenderCompID and TargetCompID are required", false;
    if (p.dictionary.empty())
      return err = "no data dictionary", false;
    std::ostringstream s;
    s << "[DEFAULT]\n"
      << "ConnectionType=initiator\n"
      << "ReconnectInterval=" << p.reconnect_s << "\n"
      << "HeartBtInt=" << p.heartbeat_s << "\n"
      << "TimeZone=America/New_York\n"
      << "StartTime=" << p.start_time << "\n"
      << "EndTime=" << p.end_time << "\n";
    if (!p.start_day.empty())
      s << "StartDay=" << p.start_day << "\nEndDay=" << p.end_day << "\n";
    s << "UseDataDictionary=Y\n"
      << "DataDictionary=" << p.dictionary << "\n"
      // LMAX asks clients to accept tags they do not know (spec, Compatibility).
      << "ValidateUserDefinedFields=N\n"
      << "AllowUnknownMsgFields=Y\n"
      << "ValidateFieldsOutOfOrder=N\n"
      << "ResetOnLogon=" << (p.reset_on_logon ? "Y" : "N") << "\n"
      << "ResetOnLogout=N\nResetOnDisconnect=N\n"
      << "PersistMessages=Y\n";
    if (!p.store_path.empty())
      s << "FileStorePath=" << p.store_path << "\n";
    if (!p.log_path.empty())
      s << "FileLogPath=" << p.log_path << "\n";
    if (p.tls)
      s << "SSLProtocol=+TLSv1.2 +TLSv1.3\n";
    s << "\n[SESSION]\n"
      << "BeginString=FIX.4.4\n"
      << "SenderCompID=" << p.sender << "\n"
      << "TargetCompID=" << p.target << "\n"
      << "SocketConnectHost=" << p.host << "\n"
      << "SocketConnectPort=" << p.port << "\n";
    out = s.str();
    return true;
  }

  namespace
  {
    bool digits(const std::string &s, size_t pos, size_t n, int &v)
    {
      if (pos + n > s.size())
        return false;
      v = 0;
      for (size_t i = pos; i < pos + n; ++i)
      {
        if (!std::isdigit((unsigned char)s[i]))
          return false;
        v = v * 10 + (s[i] - '0');
      }
      return true;
    }

    int64_t days_from_civil(int y, unsigned m, unsigned d)
    {
      y -= m <= 2;
      const int64_t era = (y >= 0 ? y : y - 399) / 400;
      const unsigned yoe = unsigned(y - era * 400);
      const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + int64_t(doe) - 719468;
    }

    // "HH:MM:SS[.f...]" at pos; ns since midnight, or -1.
    int64_t time_ns(const std::string &s, size_t pos)
    {
      int hh, mm, ss;
      if (!digits(s, pos, 2, hh) || s.size() < pos + 8 || s[pos + 2] != ':' || !digits(s, pos + 3, 2, mm) ||
          s[pos + 5] != ':' || !digits(s, pos + 6, 2, ss))
        return -1;
      int64_t ns = (int64_t(hh) * 3600 + mm * 60 + ss) * 1000000000LL;
      size_t i = pos + 8;
      if (i < s.size())
      {
        if (s[i] != '.')
          return -1;
        int64_t frac = 0, scale = 1000000000LL;
        for (++i; i < s.size(); ++i)
        {
          if (!std::isdigit((unsigned char)s[i]))
            return -1;
          scale /= 10;
          frac += (s[i] - '0') * scale;
        }
        ns += frac;
      }
      return ns;
    }

    int64_t date_ns(const std::string &s)
    {
      int y, m, d;
      if (!digits(s, 0, 4, y) || !digits(s, 4, 2, m) || !digits(s, 6, 2, d) || m < 1 || m > 12 || d < 1 || d > 31)
        return -1;
      return days_from_civil(y, m, d) * 86400LL * 1000000000LL;
    }
  }

  uint64_t utc_timestamp_ns(const std::string &s)
  {
    if (s.size() < 17 || s[8] != '-')
      return 0;
    int64_t d = date_ns(s.substr(0, 8)), t = time_ns(s, 9);
    return d < 0 || t < 0 ? 0 : uint64_t(d + t);
  }

  uint64_t utc_date_time_ns(const std::string &date, const std::string &time)
  {
    int64_t d = date_ns(date), t = time_ns(time, 0);
    return d < 0 || t < 0 ? 0 : uint64_t(d + t);
  }

  uint64_t now_ns()
  {
    using namespace std::chrono;
    return uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
  }

  bool to_steps(double value, double step, int64_t &out)
  {
    if (!(step > 0) || !std::isfinite(value))
      return false;
    const double q = value / step;
    const double r = std::round(q);
    if (std::fabs(q - r) > 1e-6)
      return false;
    out = int64_t(r);
    return true;
  }
}
