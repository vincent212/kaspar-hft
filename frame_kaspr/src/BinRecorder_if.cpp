
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/mda/act/BinRecorder.hpp"

actors::Actor* create_BinRecorder(const std::string &_outfilename)
{
  return new frame::mda::act::BinRecorder(_outfilename);
}
