#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/Actor.hpp"

namespace light::msg
{
  struct LightSetOffTR : public actors::Message_N<166>
  {
    const std::string offtr_sym;
    const std::string mat;
    const std::string cusip;
    const std::string bench_cusip;
    cfsmp offtr_px;
    LightSetOffTR(
        const std::string &offtr_sym,
        const std::string &mat,
        const std::string &cusip,
        const std::string &bench_cusip,
        cfsmp offtr_px)
        : offtr_sym(offtr_sym),
          mat(mat), cusip(cusip),
          bench_cusip(bench_cusip),
          offtr_px(offtr_px) {}
  };
}