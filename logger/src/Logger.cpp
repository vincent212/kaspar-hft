/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <boost/format.hpp>
#include "actors/msg/Start.hpp"
#include "chutil/ut.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "logger/msg/Log.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/ref/Price.hpp"
#include "chutil/Table.hpp"
#include "logger/act/Logger.hpp"
#include "chutil/places.hpp"
#include "logger/msg/CMEAudit.hpp"
#include <boost/filesystem.hpp>

using namespace std;
using namespace frame;
using namespace polonaise::logger;

// this needs to be in a separate loggger
static std::vector<std::string> get_cme_audit_headers()
{
  return {
      "Sending Timestamps",
      "Message Direction",
      "Operator ID",
      "Session ID",
      "Manual Order Indicator",
      "Message Type",
      "Instrument",
      "Order Flow ID",
      "CME Globex Order ID",
      "Client Order ID",
      "Order Request ID",
      "Buy/Sell Indicator",
      "Quantity",
      "Limit Price",
      "Stop Price",
      "Order Type",
      "Order Qualifier",
      "IFM Flag",
      "Display Quantity",
      "Minimum Quantity",
      "Country of Origin",
      "Fill Price",
      "Fill Quantity",
      "Cumulative Quantity",
      "Remaining Quantity",
      "AggressorFlag",
      "Source of Cancellation",
      "Reject Reason",
      "CME Globex Message ID",
      "Cross ID",
      "Quote Request ID",
      "Message Quote ID",
      "Quote Set ID",
      "Quote Entry ID",
      "Bid Price",
      "Bid Size",
      "Offer Price",
      "Offer Size",
      "Processed Entries",
      "Party Details List Request ID",
      "ListUpdateAction",
      "Self-Matching Prevention ID",
      "Self-Match Prevention Instr",
      "Customer Type Indicator",
      "AvgPxGroupID",
      "Average Px Ind",
      "CmtaGiveUpCD",
      "Origin",
      "Clearing Trade Price Type",
      "Customer Handling Instr",
      "Executing Firm ID",
      "Account Number",
      "Take Up Firm",
      "Take up Account"};
}

act::Logger::Logger(string _logfilename, actors::Actor *_timer)
    : flush_period(25),
      timer(_timer)
{

  // log file
  boost::format logfilenamef(".%d.%d.%d.log");
  auto t = chutil::Time::now_utc();
  logfilenamef % t.date % t.ns % getpid();
  string logfname = _logfilename + logfilenamef.str();

  // audit file
  boost::format auditfilenamef(".%d.%d.%d.audit.csv");
  auditfilenamef % t.date % t.ns % getpid();
  string auditfname = _logfilename + auditfilenamef.str();

  if (!Logger::disable)
  {
    log_file.open(logfname.c_str(), ios::out);
    if (!log_file.is_open())
      cout << "could not open " << logfname << endl;
    audit_file.open(auditfname.c_str(), ios::out);
    if (!audit_file.is_open())
      cout << "could not open " << auditfname << endl;
  }

  const auto &headers = get_cme_audit_headers();
  for (const auto &h : headers)
  {
    audit_file << h << ",";
  }
  audit_file << endl;

  tot_err_cnt = 0;
  ASSERT(theLogger == 0, "Logger already instantiated");
  theLogger = this;
  cnt = 0;
  info.set_capacity(50);
  err.set_capacity(50);
  fill.set_capacity(50);
  reject.set_capacity(50);
  timsync.set_capacity(50);
  oper.set_capacity(50);
  trd.set_capacity(50);
  omg.set_capacity(50);
}

void act::Logger::set_timer(actors::Actor *_timer) {
  timer = _timer;
}

void act::Logger::output(const string &logger_,
                         msg::Log::Level level,
                         const char *file,
                         int line,
                         const char *fmts,
                         const std::array<std::any, MAX_LOG_ARGS> &vals,
                         size_t arg_count)
{
  boost::format fmt(fmts);

  for (unsigned i = 0; i < arg_count; ++i)
  {
    if (vals[i].type() == typeid(int))
    {
      fmt % std::any_cast<int>(vals[i]);
    }
    else if (vals[i].type() == typeid(unsigned))
    {
      fmt % std::any_cast<unsigned>(vals[i]);
    }
    else if (vals[i].type() == typeid(long long))
    {
      fmt % std::any_cast<long long>(vals[i]);
    }
    else if (vals[i].type() == typeid(unsigned long long))
    {
      fmt % std::any_cast<unsigned long long>(vals[i]);
    }
    else if (vals[i].type() == typeid(long))
    {
      fmt % std::any_cast<long>(vals[i]);
    }
    else if (vals[i].type() == typeid(unsigned long))
    {
      fmt % std::any_cast<unsigned long>(vals[i]);
    }
    else if (vals[i].type() == typeid(double))
    {
      fmt % std::any_cast<double>(vals[i]);
    }
    else if (vals[i].type() == typeid(long double))
    {
      fmt % std::any_cast<long double>(vals[i]);
    }
    else if (vals[i].type() == typeid(float))
    {
      fmt % std::any_cast<float>(vals[i]);
    }
    else if (vals[i].type() == typeid(double))
    {
      fmt % std::any_cast<double>(vals[i]);
    }
    else if (vals[i].type() == typeid(const char *))
    {
      fmt % std::any_cast<const char *>(vals[i]);
    }
    else if (vals[i].type() == typeid(char *))
    {
      fmt % std::any_cast<char *>(vals[i]);
    }
    else if (vals[i].type() == typeid(char))
    {
      fmt % std::any_cast<char>(vals[i]);
    }
    else if (vals[i].type() == typeid(string))
    {
      fmt % std::any_cast<string>(vals[i]);
    }
    else if (vals[i].type() == typeid(unsigned short))
    {
      fmt % std::any_cast<unsigned short>(vals[i]);
    }
    else if (vals[i].type() == typeid(short))
    {
      fmt % std::any_cast<short>(vals[i]);
    }
    else if (vals[i].type() == typeid(bool))
    {
      fmt % std::any_cast<bool>(vals[i]);
    }
    else if (vals[i].type() == typeid(std::size_t))
    {
      fmt % std::any_cast<std::size_t>(vals[i]);
    }
    else if (vals[i].type() == typeid(frame::ref::Price))
    {
      fmt % int(std::any_cast<frame::ref::Price>(vals[i]).to_int());
    }
    else if (vals[i].type() == typeid(pint))
    {
      fmt % (int)std::any_cast<int>(vals[i]);
    }
    else if (vals[i].type() == typeid(pdouble))
    {
      fmt % (double)std::any_cast<double>(vals[i]);
    }
    else if (vals[i].type() == typeid(pos_pint))
    {
      fmt % (int)std::any_cast<int>(vals[i]);
    }
    else if (vals[i].type() == typeid(pos_pdouble))
    {
      fmt % (double)std::any_cast<double>(vals[i]);
    }
    else if (vals[i].type() == typeid(uint8_t))
    {
      fmt % (uint8_t)std::any_cast<uint8_t>(vals[i]);
    }
    else if (vals[i].type() == typeid(uint16_t))
    {
      fmt % (uint16_t)std::any_cast<uint16_t>(vals[i]);
    }
    else if (vals[i].type() == typeid(uint32_t))
    {
      fmt % (uint32_t)std::any_cast<uint32_t>(vals[i]);
    }
    else if (vals[i].type() == typeid(uint64_t))
    {
      fmt % (uint64_t)std::any_cast<uint64_t>(vals[i]);
    }
    else if (vals[i].type() == typeid(void*))
    {
      std::ostringstream oss;
      oss << std::any_cast<void*>(vals[i]);
      fmt % oss.str();
    }
    else
    {
      cerr << "unknown log variable id: "
           << i
           << vals[i].type().name() << " <--- type name "
           << logger_ << " " << fmts
           << " file: " << file
           << " line: " << line
           << endl;
      break;
    }
  }

  try
  {
    output(fmt, logger_, level, file, line);
  }
  catch (const boost::io::format_error &e)
  {
    cerr << "Boost format error: " << e.what() << '\n';
    cerr << fmt.expected_args() << std::endl;
    cerr << fmt.fed_args() << std::endl;
    cerr << "logger_: " << logger_ << std::endl;
    cerr << "level: " << level << std::endl;
    cerr << "file: " << file << std::endl;
    cerr << "line: " << line << std::endl;
  }
  catch (const std::exception &e)
  {
    cerr << "Logger exception: " << e.what() << '\n';
    cerr << fmt.expected_args() << std::endl;
    cerr << fmt.fed_args() << std::endl;
    cerr << "logger_: " << logger_ << std::endl;
    cerr << "level: " << level << std::endl;
    cerr << "file: " << file << std::endl;
    cerr << "line: " << line << std::endl;
  }
  catch (...)
  {
    cerr << "Logger exception: " << "unknown" << '\n';
    cerr << fmt.expected_args() << std::endl;
    cerr << fmt.fed_args() << std::endl;
    cerr << "logger_: " << logger_ << std::endl;
    cerr << "level: " << level << std::endl;
    cerr << "file: " << file << std::endl;
    cerr << "line: " << line << std::endl;
  }
}

void act::Logger::output(
    const boost::format &fmt,
    const string &logger_,
    msg::Log::Level level,
    const char *file,
    int line)
{

  if (disable)
    return;

  // Convert file to std::filesystem::path and take just the leaf (filename)
  std::string file_str(file);
  boost::filesystem::path p(file_str);
  std::string filename = p.filename().string();

  auto print = [this](const std::string &s)
  {
    log_file << s << endl;
  };

  string time_str;

  if (rt)
  {
    time_str = chutil::Time::now_utc().to_string(1);
  }
  else
  {
    time_str = curr_tim.to_string(1);
  }

  auto log_ = [&](const std::string &level_str)
  {
    ostringstream oss;
    oss << "[" << level_str << "] "
        << time_str
        << "- "
        << filename
        << ":" << line
        << "- "
        << logger_.c_str()
        << " " << fmt;
    print(oss.str());
    return oss.str();
  };

  try
  {

    switch (level)
    {
    case msg::Log::_DBG_:
    {
      if (!log_debug)
        break;
      log_("DEBUG");
      break;
    }
    case msg::Log::_INFO_:
    {
      log_("INFO ");
      break;
    }
    case msg::Log::_ERROR_:
    {
      tot_err_cnt++;
      std::string log_entry = log_("ERROR");
      err.push_front(log_entry);
      map<string, int>::iterator it = err_cnt.find(logger_);
      if (it == err_cnt.end())
        err_cnt[logger_] = 1;
      else
        it->second += 1;
      break;
    }
    case msg::Log::_FILL_:
    {
      std::string log_entry = log_("FILL");
      fill.push_front(log_entry);
      trd.push_front(log_entry);
      oper.push_front(log_entry);
      break;
    }
    case msg::Log::_OM_:
    {
      std::string log_entry = log_("OM   ");
      omg.push_front(log_entry);
      break;
    }
    case msg::Log::_REJECT_:
    {
      std::string log_entry = log_("REJCT");
      reject.push_front(log_entry);
      err.push_front(log_entry);
      trd.push_front(log_entry);
      oper.push_front(log_entry);
      break;
    }
    case msg::Log::_LIMIT_:
    {
      std::string log_entry = log_("LIMIT");
      err.push_front(log_entry);
      oper.push_front(log_entry);
      break;
    }
    case msg::Log::_OPER_:
    {
      std::string log_entry = log_("OPER ");
      oper.push_front(log_entry);
      break;
    }
    case msg::Log::_WARN_:
    {
      std::string log_entry = log_("WARN ");
      err.push_front(log_entry);
      break;
    }
    case msg::Log::_TIMSYNC_:
    {
      std::string log_entry = log_("TIME ");
      timsync.push_front(log_entry);
      break;
    }
    case msg::Log::_TRD_:
    {
      std::string log_entry = log_("TRADE");
      trd.push_front(log_entry);
      break;
    }
    case msg::Log::_TRACE_:
    {
      log_("TRACE");
      break;
    }

    default:
      SNGH;
    }
  }

  catch (const std::exception &e)
  {
    std::cerr << "Logger exception: " << e.what() << '\n';
    std::cerr << fmt.expected_args() << std::endl;
    std::cerr << fmt.fed_args() << std::endl;
    std::cerr << "logger_: " << logger_ << std::endl;
    std::cerr << "level: " << level << std::endl;
    std::cerr << "file: " << file << std::endl;
    std::cerr << "line: " << line << std::endl;
    std::cerr << e.what() << '\n';
  }

  if (cnt++ % flush_period)
    log_file.flush();
  else if (synchrolog)
    log_file.flush();
}

void act::Logger::output_audit(const std::vector<std::any> &vals)
{
  for (const auto &v : vals)
  {
    if (v.type() == typeid(int))
    {
      audit_file << std::any_cast<int>(v) << ",";
    }
    else if (v.type() == typeid(uint32_t))
    {
      audit_file << std::any_cast<uint32_t>(v) << ",";
    }
    else if (v.type() == typeid(uint16_t))
    {
      audit_file << int(std::any_cast<uint16_t>(v)) << ",";
    }
    else if (v.type() == typeid(uint8_t))
    {
      audit_file << int(std::any_cast<uint8_t>(v)) << ",";
    }
    else if (v.type() == typeid(unsigned))
    {
      audit_file << std::any_cast<unsigned>(v) << ",";
    }
    else if (v.type() == typeid(long long))
    {
      audit_file << std::any_cast<long long>(v) << ",";
    }
    else if (v.type() == typeid(unsigned long long))
    {
      audit_file << std::any_cast<unsigned long long>(v) << ",";
    }
    else if (v.type() == typeid(long))
    {
      audit_file << std::any_cast<long>(v) << ",";
    }
    else if (v.type() == typeid(unsigned long))
    {
      audit_file << std::any_cast<unsigned long>(v) << ",";
    }
    else if (v.type() == typeid(float))
    {
      boost::format fmt("%.8f");
      audit_file << fmt % std::any_cast<float>(v) << ",";
    }
    else if (v.type() == typeid(double))
    {
      boost::format fmt("%.8f");
      audit_file << fmt % std::any_cast<double>(v) << ",";
    }
    else if (v.type() == typeid(const char *))
    {
      audit_file << std::any_cast<const char *>(v) << ",";
    }
    else if (v.type() == typeid(char *))
    {
      audit_file << std::any_cast<char *>(v) << ",";
    }
    else if (v.type() == typeid(string))
    {
      auto outstr = std::any_cast<string>(v);
      if (outstr.size() > 0)
        audit_file << outstr << ",";
      else
        audit_file << ",";
    }
    else if (v.type() == typeid(unsigned short))
    {
      audit_file << std::any_cast<unsigned short>(v) << ",";
    }
    else if (v.type() == typeid(short))
    {
      audit_file << std::any_cast<short>(v) << ",";
    }
    else if (v.type() == typeid(bool))
    {
      audit_file << std::any_cast<bool>(v) << ",";
    }
    else if (v.type() == typeid(std::size_t))
    {
      audit_file << std::any_cast<std::size_t>(v) << ",";
    }
    else if (v.type() == typeid(chutil::Time))
    {
      audit_file << std::any_cast<chutil::Time>(v).to_string(3) << ",";
    }
    else
    {
      audit_file << /*v.type().name() <<*/ ",";
    }
  }
  audit_file << endl;
  audit_file.flush();
}

void act::Logger::process_message(cmsgt m)
{
  if (typeid(*m) == typeid(logger::msg::Log))
  {
    auto msg = static_cast<const msg::Log *>(m);
    try
    {
      output(msg->logger, msg->level, msg->file, msg->line, msg->fmts, msg->val_arr, msg->arg_count);
    }

    catch (const boost::io::format_error &e)
    {
      std::cerr << msg->logger << " " << msg->level << " " << msg->file << " " << msg->line << std::endl;
      std::cerr << "Boost format error: " << e.what() << '\n';
      std::cerr << msg->fmts << std::endl;
      std::cerr << msg->val_arr.size() << std::endl;
    }

    catch (const std::exception &e)
    {
      std::cerr << msg->logger << " " << msg->level << " " << msg->file << " " << msg->line << std::endl;
      std::cerr << "Logger exception: " << e.what() << '\n';
    }

    catch (...)
    {
      std::cerr << msg->logger << " " << msg->level << " " << msg->file << " " << msg->line << std::endl;
      std::cerr << "Logger exception: " << "unknown" << '\n';
    }
  }
  else if (typeid(*m) == typeid(logger::msg::CMEAudit))
  {
    auto msg = static_cast<const msg::CMEAudit *>(m);
    output_audit(msg->val_arr);
  }
  else if (typeid(*m) == typeid(mtim::msg::Alarm))
  {
    auto msg = static_cast<const mtim::msg::Alarm *>(m);
    curr_tim = msg->currtim;
    log_file.flush();
  }
  else if (typeid(*m) == typeid(actors::msg::Start))
  {
    if (timer)
    {
      timer->send(new mtim::msg::AlarmClockSub(0, 250, 0, true), this);
    }
  }
  else if (typeid(*m) == typeid(cons::msg::Get))
  {
    auto msg = static_cast<const cons::msg::Get *>(m);
    if (msg->what == "inf")
    {
      reply(new cons::msg::Page(getdata(info, "Info")));
    }
    else if (msg->what == "err")
    {
      reply(new cons::msg::Page(getdata(err, "Errors")));
    }
    else if (msg->what == "fil")
    {
      reply(new cons::msg::Page(getdata(fill, "Fills")));
    }
    else if (msg->what == "rej")
    {
      reply(new cons::msg::Page(getdata(reject, "Rejects")));
    }
    else if (msg->what == "opr")
    {
      reply(new cons::msg::Page(getdata(oper, "Oper")));
    }
    else if (msg->what == "tim")
    {
      reply(new cons::msg::Page(getdata(timsync, "Time")));
    }
    else if (msg->what == "trd")
    {
      reply(new cons::msg::Page(getdata(trd, "Trader")));
    }
    else if (msg->what == "omg")
    {
      reply(new cons::msg::Page(getdata(omg, "OM")));
    }
    else
    {
      reply(new cons::msg::Page("unknown command"));
    }
  }
}

string
act::Logger::getdata(const boost::circular_buffer<std::string> &v, const string &title)
{
  std::size_t len = v.size();
  vector<string> cols;
  cols.push_back("log");
  chutil::Table t(title, cols);
  for (std::size_t i = 0; i < len; ++i)
  {
    t.add_row();
    auto s = boost::algorithm::trim_right_copy(v[i]);
    t.set_value(v[i]);
  }
  return t.to_string();
}

alignas(64) polonaise::logger::act::Logger *polonaise::logger::act::Logger::theLogger = 0;
alignas(64) bool polonaise::logger::act::Logger::log_debug = true;
alignas(64) bool polonaise::logger::act::Logger::synchrolog = false;
alignas(64) bool polonaise::logger::act::Logger::rt = false;
alignas(64) bool polonaise::logger::act::Logger::disable = false;
