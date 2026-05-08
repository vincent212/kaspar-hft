#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <boost/format.hpp>

namespace chutil
{

  static
  std::string convert_price_to_32nds(double price)
  {
    // Calculate the whole part of the price
    int whole_part = static_cast<int>(price);

    // Calculate the fractional part in 32nds
    double fractional_part = price - whole_part;
    int thirty_seconds = static_cast<int>(fractional_part * 32);

    // Calculate the 8ths of a 32nd
    double eighths_of_32nd = (fractional_part * 32 - thirty_seconds) * 8;

    // Format the price
    std::string formatted_price = (boost::format("%03d-%02d-%.1f") % whole_part % thirty_seconds % eighths_of_32nd).str();
    return formatted_price;
  }

} // namespace chutil