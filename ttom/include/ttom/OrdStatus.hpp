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
   * @brief Order status values for tag 39
   * @see  https://library.tradingtechnologies.com/tt-fix/gateway/Msg_ExecutionReport_8.html
   *
   */
  enum class OrdStatus
  {
    NewOrder = 0,
    Partially_filled, // 1
    Filled,           // 2
    Done_for_day,     // 3
    Canceled,         // 4
    Replaced,         // 5
    Pending_Cancel,   // 6
    Rejected = 8,
    Pending_New = 10,    // A
    Expired = 12,        // C
    Pending_Replace = 15,// E
    UNKNOWN=99
  };

  static OrdStatus convertOrdStatus(const std::string &s)
  {
    switch (s.c_str()[0])
    {
    case '0':
      return OrdStatus::NewOrder;
    case '1':
      return OrdStatus::Partially_filled;
    case '2':
      return OrdStatus::Filled;
    case '3':
      return OrdStatus::Done_for_day;
    case '4':
      return OrdStatus::Canceled;
    case '5':
      return OrdStatus::Replaced;
    case '6':
      return OrdStatus::Pending_Cancel;
    case '8':
      return OrdStatus::Rejected;
    case 'A':
      return OrdStatus::Pending_New;
    case 'C':
      return OrdStatus::Expired;
    case 'E':
      return OrdStatus::Pending_Replace;
    default:
    {
      std::cerr << "*** unknown OrdStatus " << s << std::endl;
      return OrdStatus::UNKNOWN;
    }
    }
  }

  static std::string to_string(OrdStatus s)
  {
    switch (s)
    {
    case OrdStatus::NewOrder:
      return "OrdStatus::NewOrder";
    case OrdStatus::Partially_filled:
      return "OrdStatus::Partially_filled";
    case OrdStatus::Filled:
      return "OrdStatus::Filled";
    case OrdStatus::Done_for_day:
      return "OrdStatus::Done_for_day";
    case OrdStatus::Canceled:
      return "OrdStatus::Canceled";
    case OrdStatus::Replaced:
      return "OrdStatus::Replaced";
    case OrdStatus::Pending_Cancel:
      return "OrdStatus::Pending_Cancel";
    case OrdStatus::Rejected:
      return "OrdStatus::Rejected";
    case OrdStatus::Pending_New:
      return "OrdStatus::Pending_New";
    case OrdStatus::Expired:
      return "OrdStatus::Expired";
    case OrdStatus::Pending_Replace:
      return "OrdStatus::Pending_Replace";
    case OrdStatus::UNKNOWN:
      return "OrdStatus::UNKNOWN";
    default:
      throw std::runtime_error("cannot convert OrdStatus to string");
    }
  }

}