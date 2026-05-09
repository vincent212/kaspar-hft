<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Main Program Setup Standard

All main programs in m2_kaspr follow a standard setup pattern for signal handling, argument parsing, and initialization.

## Required Headers

```cpp
#include "chutil/Macros.hpp"      // For SET_ARGS, PRINT_ARGS
#include "chutil/sig_hand.hpp"    // For my_signal_handler
#include "chutil/Assert.hpp"      // For DECL_PARAM macros

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <signal.h>
#include <fenv.h>   // For floating point exceptions

namespace po = boost::program_options;
using namespace std;
```

## Standard main() Template

```cpp
int main(int argc, char *argv[])
{
  // 1. Save args for debugging (REQUIRED)
  SET_ARGS;
  PRINT_ARGS;

  // 2. Install signal handlers for crash debugging (REQUIRED)
  ::signal(SIGSEGV, &my_signal_handler);  // Segmentation fault
  ::signal(SIGABRT, &my_signal_handler);  // Abort
  ::signal(SIGBUS,  &my_signal_handler);  // Bus error
  ::signal(SIGFPE,  &my_signal_handler);  // Floating point exception
  ::signal(SIGPIPE, &my_signal_handler);  // Broken pipe (core dump)
  // Or to ignore SIGPIPE: ::signal(SIGPIPE, SIG_IGN);

  // 3. Print startup info
  cout << "starting myprogram" << endl;
  cout << __FILE__ << endl;
  cout << argv[0] << endl;
  cout << boost::filesystem::current_path() << endl;
  cout << "pid: " << getpid() << endl;

  // 4. Parse command line arguments
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
    ("logdebug", "enable debug logs")
    ("inifile", po::value<string>(), "main ini file")
    ("count", po::value<int>(), "optional count")
  ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    cout << desc << "\n";
    return 1;
  }

  // 5. Extract required parameters
  DECL_PARAM(inifile, std::string);

  // 6. Extract optional parameters
  DECL_PARAM_OPT(count, int);  // Declares: int count; bool has_count = false;
  if (has_count) {
    cout << "Using count: " << count << endl;
  }

  // 7. Configure logger (REQUIRED)
  if (vm.count("logdebug")) {
    polonaise::logger::act::Logger::synchrolog = true;
    polonaise::logger::act::Logger::log_debug = true;
  } else {
    polonaise::logger::act::Logger::log_debug = false;
    polonaise::logger::act::Logger::synchrolog = false;
  }

  // 8. Create and run the manager
  auto man = new MyManager(inifile);

  // 9. Enable floating point exception trapping (REQUIRED)
  feenableexcept(FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INVALID);

  // 10. Run in thread and wait
  auto thrd = boost::thread(boost::ref(*man));
  thrd.join();

  return 0;
}
```

## Signal Handler Details

The `my_signal_handler` function (from `chutil/sig_hand.hpp`):

1. **Writes debug file**: Creates `./<prefix>_<signum>_<id>_<pid>.args` containing:
   - Current UTC timestamp
   - Assertion message (if assertion failed)
   - Signal number
   - Full command line arguments
   - C++23 stack trace

2. **Signal types**:
   - `SIGSEGV` (11): Segmentation fault - invalid memory access
   - `SIGABRT` (6): Abort - called by assert() or abort()
   - `SIGBUS` (7): Bus error - memory alignment issues
   - `SIGFPE` (8): Floating point exception - div by zero, overflow
   - `SIGPIPE` (13): Broken pipe - writing to closed socket

3. **Requires SET_ARGS**: The handler uses `_x_argc` and `_x_argv` globals set by `SET_ARGS` macro

## Macros Reference

| Macro | Description |
|-------|-------------|
| `SET_ARGS` | Saves argc/argv to globals for debugging |
| `PRINT_ARGS` | Prints command line to stdout and stderr |
| `DECL_PARAM(name, type)` | Required parameter - exits if missing |
| `DECL_PARAM_OPT(name, type)` | Optional parameter - declares `has_name` bool |
| `ASSERT(cond, msg)` | Assert with message, prints args on fail |
| `ASSERTF(cond, bfmsg)` | Assert with boost::format message |

## DECL_PARAM vs DECL_PARAM_OPT

**DECL_PARAM(name, type)** - Required parameter:
```cpp
DECL_PARAM(inifile, std::string);
// Declares: std::string inifile;
// If missing: prints "missing inifile" and returns 1
// If present: prints "inifile = value" and sets variable
```

**DECL_PARAM_OPT(name, type)** - Optional parameter:
```cpp
DECL_PARAM_OPT(count, int);
// Declares: int count; bool has_count = false;
// If missing: does nothing (variable is default-initialized)
// If present: prints "count = value", sets variable, sets has_count = true

if (has_count) {
  // Use count
}
```

## Why This Matters

1. **Crash debugging**: Signal handlers capture stack traces and args
2. **Reproducibility**: Args are logged for recreating issues
3. **Consistent startup**: All programs follow same pattern
4. **FE exceptions**: Catch floating point errors (div by zero, overflow, etc.) immediately instead of silently producing NaN/Inf
5. **Logger control**: `--logdebug` flag enables synchronous debug logging for troubleshooting
