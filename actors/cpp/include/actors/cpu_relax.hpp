#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <thread>

namespace actors
{
  // A hint to the core inside a spin loop: PAUSE on x86, YIELD on ARM, else a
  // scheduler yield. The one copy, shared by the mailboxes, the actor run loop
  // and the FPGA link (actors/fpga/include/kfpga/ring.hpp).
  inline void cpu_relax() noexcept
  {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    std::this_thread::yield();
#endif
  }
}
