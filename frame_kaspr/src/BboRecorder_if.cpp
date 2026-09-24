/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/ob/act/BboRecorder.hpp"
#include "frame/ob/if/BboRecorder.hpp"

actors::Actor* create_BboRecorder(
    const std::string &_outfilename,
    const std::string &_session_date,
    const std::vector<actors::Actor*> &_obs)
{
  return new frame::ob::act::BboRecorder(_outfilename, _session_date, _obs);
}
