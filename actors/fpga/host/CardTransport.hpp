#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * CardTransport -- how envelopes move between the CPU and the FPGA card.
 *
 * write() puts one envelope into the card's from_host stream; read() takes one
 * envelope from its to_pcie stream if there is one. On a real card this is the
 * PCIe DMA path (host-memory rings, QDMA streams, ...); SoftCard runs the FPGA
 * design in software threads instead.
 */

#include "actors_fpga/envelope.hpp"

namespace kfpga {

class CardTransport
{
public:
  virtual ~CardTransport() = default;
  virtual void write(const Envelope &e) = 0;
  virtual bool read(Envelope &e) = 0;   // non-blocking
};

} // namespace kfpga
