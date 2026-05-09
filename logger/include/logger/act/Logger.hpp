#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <fstream>
#include <deque>
#include <any>
#include <array>
#include <algorithm>
#include <type_traits>

#include "chutil/Time.hpp"
#include "actors/Actor.hpp"
#include "logger/msg/Log.hpp"
#include <boost/circular_buffer.hpp>
#include <boost/algorithm/string/trim.hpp>

#if defined(DEBUG) || defined(_DEBUG) || defined(LOGDEBUG)
#define log_dbg(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_DBG_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#else
#define log_dbg(...) \
  {                  \
  }
#endif
#define log_inf(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_INFO_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_err(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_ERROR_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_fil(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_FILL_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_rej(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_REJECT_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_lim(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_LIMIT_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_opr(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_OPER_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_wrn(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_WARN_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#if defined(TIMTRACE)
#define log_tim(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_TIMSYNC_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#else
#define log_tim(...) \
  {                  \
  }
#endif
#define log_trd(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_TRD_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#define log_omg(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_OM_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)

#define MAX_LOG_ARGS 16
#if defined(TRACE)
#define log_trc(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_TRACE_, (get_name()), __FILE__, __LINE__, __VA_ARGS__)
#else
#define log_trc(...) \
  {                  \
  }
#endif

namespace polonaise
{
  namespace logger
  {
    namespace act
    {

      class Logger : public actors::Actor
      {
      public:
        alignas(64) std::map<std::string, int> err_cnt;
        alignas(64) uint tot_err_cnt;
        alignas(64) static Logger *theLogger;
        alignas(64) static bool log_debug;
        alignas(64) static bool synchrolog;
        alignas(64) static bool rt;
        alignas(64) static bool disable;
        alignas(64) boost::circular_buffer<std::string> info;
        alignas(64) boost::circular_buffer<std::string> err;
        alignas(64) boost::circular_buffer<std::string> fill;
        alignas(64) boost::circular_buffer<std::string> reject;
        alignas(64) boost::circular_buffer<std::string> timsync;
        alignas(64) boost::circular_buffer<std::string> oper;
        alignas(64) boost::circular_buffer<std::string> trd;
        alignas(64) boost::circular_buffer<std::string> omg;
        alignas(64) uint cnt;

        static Logger *instance()
        {
          return theLogger;
        }

        alignas(64) actors::Actor *timer;
        alignas(64) uint flush_period;

        Logger(std::string = "log", actors::Actor *_timer = 0);

        void set_timer(actors::Actor *_timer);

      private:
        std::ofstream log_file;
        std::ofstream audit_file;

        chutil::Time curr_tim;

        void output(const std::string &logger_,
                    msg::Log::Level level,
                    const char *file,
                    int line,
                    const char *fmts,
                    const std::array<std::any, MAX_LOG_ARGS> &vals,
                    size_t arg_count);

        void output(
          const boost::format &fmt, 
          const std::string &logger, 
          msg::Log::Level level, 
          const char *file,
          int line);

        void output_audit(const std::vector<std::any>&);

      protected:
        void process_message(cmsgt m);
        std::string getdata(const boost::circular_buffer<std::string> &v, const std::string &title);
        const char* get_name() const { return "Logger"; }
      };

      template <typename T, typename... Values>
      void
      unpack_macro(std::array<std::any, MAX_LOG_ARGS> &val_arr, size_t &arg_count, const char *&fmt, const T &_fmt, Values... values)
      {
        static_assert(sizeof...(values) <= MAX_LOG_ARGS, "Too many log arguments");
        fmt = &_fmt[0];
        arg_count = sizeof...(values);
        if constexpr (sizeof...(values) <= MAX_LOG_ARGS) {
          size_t index = 0;
          ((val_arr[index++] = values), ...);
        }
      }

      template <typename T>
      void
      unpack_macro(std::array<std::any, MAX_LOG_ARGS> &, size_t &arg_count, const char *&fmt, const T &_fmt)
      {
        fmt = &_fmt[0];
        arg_count = 0;
      }

      // Fast path specialization for string-only messages (no formatting)
      inline void log(
          msg::Log::Level level,
          const char* name,
          const char *file,
          int line,
          const char *message)
      {
        if (level == msg::Log::Level::_DBG_ && !Logger::log_debug) [[unlikely]]
          return;

        if (Logger::theLogger)
        {
          // No std::any overhead, direct string handling
          std::array<std::any, MAX_LOG_ARGS> val_arr{};
          if (Logger::synchrolog) [[unlikely]]
          {
            msg::Log log_msg(level, name, message, val_arr, file, line);
            Logger::theLogger->fast_send(&log_msg, nullptr);
          }
          else
          {
            msg::Log *msg = new msg::Log(level, name, message, val_arr, file, line);
            Logger::theLogger->send(msg, nullptr);
          }
        }
      }

      // Fast path specialization for format + 1 integer argument
      template<typename T>
      inline typename std::enable_if<std::is_integral_v<T>, void>::type
      log(
          msg::Log::Level level,
          const char* name,
          const char *file,
          int line,
          const char *fmt,
          T value)
      {
        if (level == msg::Log::Level::_DBG_ && !Logger::log_debug) [[unlikely]]
          return;

        if (Logger::theLogger)
        {
          // Optimized single-argument path
          std::array<std::any, MAX_LOG_ARGS> val_arr{};
          val_arr[0] = static_cast<int>(value);
          if (Logger::synchrolog) [[unlikely]]
          {
            msg::Log log_msg(level, name, fmt, val_arr, file, line, 1);
            Logger::theLogger->fast_send(&log_msg, nullptr);
          }
          else
          {
            msg::Log *msg = new msg::Log(level, name, fmt, val_arr, file, line, 1);
            Logger::theLogger->send(msg, nullptr);
          }
        }
      }

      // Fast path specialization for format + 1 double argument
      inline void log(
          msg::Log::Level level,
          const char* name,
          const char *file,
          int line,
          const char *fmt,
          double value)
      {
        if (level == msg::Log::Level::_DBG_ && !Logger::log_debug) [[unlikely]]
          return;

        if (Logger::theLogger)
        {
          // Optimized single-argument path
          std::array<std::any, MAX_LOG_ARGS> val_arr{};
          val_arr[0] = value;
          if (Logger::synchrolog) [[unlikely]]
          {
            msg::Log log_msg(level, name, fmt, val_arr, file, line, 1);
            Logger::theLogger->fast_send(&log_msg, nullptr);
          }
          else
          {
            msg::Log *msg = new msg::Log(level, name, fmt, val_arr, file, line, 1);
            Logger::theLogger->send(msg, nullptr);
          }
        }
      }

      // Fast path specialization for format + 1 string argument
      inline void log(
          msg::Log::Level level,
          const char* name,
          const char *file,
          int line,
          const char *fmt,
          const std::string &value)
      {
        if (level == msg::Log::Level::_DBG_ && !Logger::log_debug) [[unlikely]]
          return;

        if (Logger::theLogger)
        {
          // Optimized single-argument path
          std::array<std::any, MAX_LOG_ARGS> val_arr{};
          val_arr[0] = value;
          if (Logger::synchrolog) [[unlikely]]
          {
            msg::Log log_msg(level, name, fmt, val_arr, file, line, 1);
            Logger::theLogger->fast_send(&log_msg, nullptr);
          }
          else
          {
            msg::Log *msg = new msg::Log(level, name, fmt, val_arr, file, line, 1);
            Logger::theLogger->send(msg, nullptr);
          }
        }
      }

      // General template for multiple arguments (fallback)
      template <typename... Values>
      void log(
          msg::Log::Level level,
          const char* name,
          const char *file,
          int line,
          const Values &...values)
      {
        if (level == msg::Log::Level::_DBG_ && !Logger::log_debug) [[unlikely]]
          return;

        std::array<std::any, MAX_LOG_ARGS> val_arr;
        size_t arg_count;
        const char *fmt;
        unpack_macro(val_arr, arg_count, fmt, values...);
        if (Logger::theLogger)
        {
          if (Logger::synchrolog) [[unlikely]]
          {
            msg::Log log_msg(level, name, fmt, val_arr, file, line, arg_count);
            Logger::theLogger->fast_send(&log_msg, nullptr);
          }
          else
          {
            msg::Log *msg = new msg::Log(level, name, fmt, val_arr, file, line, arg_count);
            Logger::theLogger->send(msg, nullptr);
          }
        }
      }

    }
  }
}
