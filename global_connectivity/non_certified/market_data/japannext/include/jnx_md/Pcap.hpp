#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The pcap reader for replay and the sample-data tests: the parser shared by
// the handlers under market_data/ (../pcap_file.hpp).

#include "pcap_file.hpp"

namespace jnx_md
{
  using market_data::Pcap;
  using market_data::PcapPacket;
}
