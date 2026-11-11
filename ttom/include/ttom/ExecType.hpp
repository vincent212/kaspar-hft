#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <stdexcept>
#include <iostream>

namespace ttfixom
{

  /**
   * @brief Exec Type (reason for message) tag 150
   * @see https://library.tradingtechnologies.com/tt-fix/gateway/Msg_ExecutionReport_8.html
   * @see https://www.onixs.biz/fix-dictionary/4.4/tagNum_150.html
   *
   */
  enum class ExecType
  {
    NewOrder = 0,
    Partially_filled, // 1 (deprecated)
    Filled,           // 2 (deprecated)
    Done_for_the_day,
    Canceled, // 4
    Replaced, // 5
    Rejected = 8,
    Pending_New = 10,
    Expired = 12,             // C
    Restated = 13,            // D
    Pending_Replace = 14,     // E
    Trade = 15,               // F
    Trade_Correct = 16,       // G
    Trade_Cancel = 17,        // H
    Trigerred_Activated = 21, // L
    UNKONWN = 99
  };

  static ExecType convertExecType(const std::string &s)
  {
    switch (s.c_str()[0])
    {
    case '0':
      return ExecType::NewOrder;
    case '1':
      return ExecType::Partially_filled;
    case '2':
      return ExecType::Filled;
    case '3':
      return ExecType::Done_for_the_day;
    case '4':
      return ExecType::Canceled;
    case '5':
      return ExecType::Replaced;
    case '8':
      return ExecType::Rejected;
    case 'A':
      return ExecType::Pending_New;
    case 'D':
      return ExecType::Restated;
    case 'E':
      return ExecType::Pending_Replace;
    case 'F':
      return ExecType::Trade;
    case 'G':
      return ExecType::Trade_Correct;
    case 'H':
      return ExecType::Trade_Cancel;
    case 'L':
      return ExecType::Trigerred_Activated;
    default:
    {
      std::cerr << "***> unknown ExecType: " << s << std::endl;
      return ExecType::UNKONWN;
    }
    }
  }

  static std::string to_string(ExecType x)
  {
    switch (x)
    {
    case ExecType::NewOrder:
      return "ExecType::NewOrder";
    case ExecType::Partially_filled:
      return "ExecType::Partially_filled";
    case ExecType::Filled:
      return "ExecType::Filled";
    case ExecType::Done_for_the_day:
      return "ExecType::Done_for_the_day";
    case ExecType::Canceled:
      return "ExecType::Canceled";
    case ExecType::Replaced:
      return "ExecType::Replaced";
    case ExecType::Rejected:
      return "ExecType::Rejected";
    case ExecType::Pending_New:
      return "ExecType::Pending_New";
    case ExecType::Restated:
      return "ExecType::Restarted";
    case ExecType::Pending_Replace:
      return "ExecType::Pending_Replace";
    case ExecType::Trade:
      return "ExecType::Trade";
    case ExecType::Trade_Correct:
      return "ExecType::Trade_Correct";
    case ExecType::Trade_Cancel:
      return "ExecType::Trade_Cancel"; 
    case ExecType::Trigerred_Activated:
      return "ExecType::Trigerred_Activated"; 
    case ExecType::UNKONWN:
      return "ExecType::UNKONWN";
    default:
      throw std::runtime_error("cant convert to string ExecType");
    }
  }

}