/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The MDP3 decoder as hardware (phase 1 of MDP3_PLAN.md): UDP payloads in
// as 8-byte AXI-Stream words, decoder events out as envelopes. n_packets
// packets per call, the counted boundary of HLS.md section 3.

#include "mdp3fpga/decoder.hpp"

void mdp3_decoder_top(hls::stream<mdp3fpga::InWord> &in, hls::stream<kfpga::Envelope> &out, uint32_t n_packets)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
packets:
  for (uint32_t p = 0; p < n_packets; ++p)
    mdp3fpga::decode_packet(in, out);
}
