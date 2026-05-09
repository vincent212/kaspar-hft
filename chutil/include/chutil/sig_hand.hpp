#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <iostream>
#include <signal.h>     // ::signal, ::raise
#include <boost/format.hpp>
#include <string>
#include <fstream>

static int _id = 0;

#if __cpp_lib_stacktrace >= 202011L
#include <stacktrace>
#else
#include <execinfo.h>
#endif
#include "chutil/Time.hpp"



static void my_signal_handler(int signum) {
  ::signal(signum, SIG_DFL);
  extern bool _x_assertion_failed__;
  extern bool _x_manager_shutdown__;
  extern char _x_assert_msg_[10000];;
  std::string prefix="sig_";
  if (_x_assertion_failed__)
    prefix="assertion_";
  else if (_x_manager_shutdown__)
    prefix="shutdown_";

  auto fname = boost::format("./%s_%d_%d_%d.args") % prefix % signum % _id % getpid();
  std::ofstream out(fname.str());

  out << chutil::Time::now_utc().to_string() << std::endl;

  if (_x_assertion_failed__) {
    out << "ASSERTION FAILED\n";
    out << std::string(_x_assert_msg_) << std::endl;
  } else if (_x_manager_shutdown__) {
    out << "SIG ON SHUTDOWN\n";
  }
  else if (signum == SIGPIPE) {
    out << "SIG PIPE (aborting)\n";
  }
  else if (signum == SIGFPE) {
    out << "SIG FPE (aborting)\n";
  }

  out << "signum = " << signum << "\n";
  for (int i = 0; i < _x_argc; i++)
  {
    out << _x_argv[i] << " ";
  }

  // Print call stack
  try {
    out << "\nCall stack:\n";
#if __cpp_lib_stacktrace >= 202011L
    auto st = std::stacktrace::current();
    for (const auto& frame : st) {
      out << frame << "\n";
    }
#else
    void* frames[64];
    int nframes = backtrace(frames, 64);
    char** symbols = backtrace_symbols(frames, nframes);
    if (symbols) {
      for (int i = 0; i < nframes; i++) {
        out << symbols[i] << "\n";
      }
      free(symbols);
    }
#endif
  } catch (const std::exception& e) {
    out << "exception in printing stack trace: " << e.what() << "\n";
  } catch (...) {
    out << "unknown exception in printing stack trace\n";
  }

  out << "\n";
  out.close();

  if (signum == SIGPIPE || signum == SIGFPE)
    ::raise(SIGABRT);
  else
    ::raise(signum);
}
