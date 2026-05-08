#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <boost/current_function.hpp>

#define OOOGSL_EXCEPT(cond,msg) {if (!(cond)) throw std::runtime_error(msg);}

// BOOST_ENABLE_ASSERT_DEBUG_HANDLER is defined for the whole project
#include <stdexcept>    // std::logic_error
#include <iostream>     // std::cerr

namespace boost {
  inline void assertion_failed_msg(char const* expr, char const* msg, char const* function, char const* /*file*/, long /*line*/) {
    std::cerr << "Expression '" << expr << "' is false in function '" << function << "': " << (msg ? msg : "<...>") << ".\n";
    std::abort();
  }

  inline void assertion_failed(char const* expr, char const* function, char const* file, long line) {
    ::boost::assertion_failed_msg(expr, 0 /*nullptr*/, function, file, line);
  }
} // namespace boost

#define CHLIKELY(x)       (__builtin_expect(!!(x),1))
#define CHUNLIKELY(x)     (__builtin_expect(!!(x),0))

// args for debugging
extern int _x_argc;
extern char**_x_argv;
extern bool _x_assertion_failed__;
extern char _x_assert_msg_[10000];

#define SET_ARGS {_x_argc=argc;_x_argv=argv;}

#define PRINT_ARGS \
{\
fprintf(stdout, "ARGS: "); \
fprintf(stderr, "ARGS: "); \
for (int ___i = 0; ___i < _x_argc; ___i++)\
{\
  fprintf(stdout, "%s ",  _x_argv[___i]);   \
  fprintf(stderr, "%s ",  _x_argv[___i]);   \
}\
fprintf(stderr, "\n"); \
fprintf(stdout, "\n"); \
}

#define DECL_PARAM(nam,vtyp) \
vtyp nam; \
if (vm.count(#nam)) { \
nam = vm[#nam].as<vtyp>(); \
std::cout << #nam << " = " << nam << std::endl; \
} \
else { \
std::cout << "missing " << #nam << endl; \
return 1; \
}

#define DECL_PARAM_OPT(nam,vtyp) \
vtyp nam; \
[[maybe_unused]]bool has_##nam = false; \
if (vm.count(#nam)) { \
nam = vm[#nam].as<vtyp>(); \
std::cout << #nam << " = " << nam << std::endl; \
has_##nam = true; \
} \
else { }

#define FK(__map, __k) \
  [](const auto &m, const auto &k) \
  { \
    auto p = m.find(k); \
    if (p==m.end()) ERR("key not found"); \
    return p->second; \
  } (__map, __k)


#ifdef NOASSERT
#define ASSERT(cond,msg) {}
#define ASSERTF(cond,msg) {}
#else

#ifndef ASSERT

#define ASSERTF(cond,bfmsg) {if(CHUNLIKELY(!(cond))) {\
  sprintf(_x_assert_msg_, "ASSERT %s in %s> %s:%d %s\n", \
  __STRING(cond),\
  BOOST_CURRENT_FUNCTION,__FILE__,__LINE__,str(bfmsg).c_str());\
  PRINT_ARGS; \
  fprintf(stderr,"%s",_x_assert_msg_); \
  fprintf(stdout,"%s",_x_assert_msg_); \
  fflush(stdout); \
  _x_assertion_failed__ = true; \
  abort();}}

#define ASSERT(cond,msg) {if(CHUNLIKELY(!(cond))) {\
  sprintf(_x_assert_msg_, "ASSERT %s in %s> %s:%d %s\n", \
  __STRING(cond),\
  BOOST_CURRENT_FUNCTION,__FILE__,__LINE__,(msg));\
  PRINT_ARGS; \
  fprintf(stderr,"%s",_x_assert_msg_); \
  fprintf(stdout,"%s",_x_assert_msg_); \
  fflush(stdout); \
  _x_assertion_failed__ = true; \
  abort();}}
#endif


#endif
