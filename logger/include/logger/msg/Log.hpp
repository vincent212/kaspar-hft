#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "boost/format.hpp"
#include <string>
#include <any>
#include <array>

#define MAX_LOG_ARGS 16

namespace polonaise
{
  namespace logger
  {
    namespace msg
    {

      class Log : public actors::Message_N<16>  , public actors::MemoryPool<Log,64,64,4096>
      {

      public:

        typedef enum {_DBG_, _INFO_, _ERROR_, _FILL_,
         _REJECT_, _LIMIT_, _OPER_, _WARN_, _TIMSYNC_, _TRD_, _TRACE_, _OM_
         } Level;

        Log(Level _level,
          const char* _logger,
          const char*_fmts,
          const std::array<std::any, MAX_LOG_ARGS>&_val_arr,
          const char*_file,
          int _line,
          size_t _arg_count = 0)
          :logger(_logger),level(_level),fmts(_fmts),val_arr(_val_arr),
          file(_file),line(_line),arg_count(_arg_count)
        {
        }

        int line;
        const char*fmts;
        const char*file;
        const char* logger;
        std::array<std::any, MAX_LOG_ARGS> val_arr;
        Level level;
        size_t arg_count;

      };

    }

  }

}


