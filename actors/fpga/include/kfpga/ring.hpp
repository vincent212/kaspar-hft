#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * Ring - a fixed array of 64-byte envelope slots with one writer and one
 * reader. In software it stands for all three kinds of channel:
 *
 *   - a FIFO between two FPGA processes (hls::stream on the chip),
 *   - a window the CPU writes into and the card reads,
 *   - a lane in host memory the card writes into and a CPU thread polls.
 *
 * It follows the rules for rings shared between a CPU and a card:
 *
 *   - write across, read locally: the writer never reads a slot, the reader
 *     never writes one;
 *   - sequence number last: the writer copies the envelope, then stores its
 *     seq with release ordering; the reader loads the seq with acquire
 *     ordering and takes the slot once it sees the number it expects;
 *   - one message per 64-byte slot;
 *   - batched position updates: the reader publishes how far it has read
 *     every quarter ring, or when it finds the ring empty; the writer keeps a
 *     copy and refreshes it only when the ring looks full.
 *
 * A full ring stalls only its writer, as a full hardware FIFO does.
 * Software only, kept to C++14 like the rest of the runtime.
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

#include "kfpga/envelope.hpp"

namespace kfpga {

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

class Ring
{
  static constexpr size_t kBody = offsetof(Envelope, seq);   // header + payload

  Envelope *slots_ = nullptr;
  uint32_t cap_ = 0;
  uint32_t mask_ = 0;
  uint32_t batch_ = 1;

  // writer's line
  alignas(64) uint32_t tail_ = 0;        // next position to write
  uint32_t head_seen_ = 0;               // the reader's position, as last published
  // reader's line
  alignas(64) uint32_t head_ = 0;        // next position to read
  uint32_t unpublished_ = 0;
  // written by the reader, read by the writer only when the ring looks full
  alignas(64) std::atomic<uint32_t> head_pub_{0};

  static uint32_t round_up_pow2(size_t n)
  {
    uint32_t p = 1;
    while (p < n)
      p <<= 1;
    return p;
  }

  void publish() noexcept
  {
    head_pub_.store(head_, std::memory_order_release);
    unpublished_ = 0;
  }

public:
  explicit Ring(size_t depth = 16)
    : cap_(round_up_pow2(depth < 2 ? 2 : depth)), mask_(cap_ - 1), batch_(cap_ / 4 ? cap_ / 4 : 1)
  {
    void *p = nullptr;
    if (posix_memalign(&p, 64, cap_ * sizeof(Envelope)) != 0)
      throw std::bad_alloc();
    slots_ = static_cast<Envelope *>(p);
    for (uint32_t i = 0; i < cap_; ++i)
      new (&slots_[i]) Envelope();   // seq 0: nothing written yet
  }
  ~Ring() { std::free(slots_); }
  Ring(const Ring &) = delete;
  Ring &operator=(const Ring &) = delete;

  size_t depth() const noexcept { return cap_; }

  // ---- writer side ----

  bool full() noexcept
  {
    if (tail_ - head_seen_ < cap_)
      return false;
    head_seen_ = head_pub_.load(std::memory_order_acquire);
    return tail_ - head_seen_ >= cap_;
  }

  bool write_nb(const Envelope &e) noexcept
  {
    if (full())
      return false;
    Envelope &s = slots_[tail_ & mask_];
    std::memcpy(static_cast<void *>(&s), &e, kBody);
    ++tail_;
    __atomic_store_n(&s.seq, tail_, __ATOMIC_RELEASE);   // last
    return true;
  }

  void write(const Envelope &e) noexcept
  {
    while (!write_nb(e))
      cpu_relax();
  }

  // ---- reader side ----

  bool empty() noexcept
  {
    const Envelope &s = slots_[head_ & mask_];
    if (__atomic_load_n(&s.seq, __ATOMIC_ACQUIRE) == head_ + 1)
      return false;
    if (unpublished_)
      publish();   // let a waiting writer see the room
    return true;
  }

  bool read_nb(Envelope &out) noexcept
  {
    if (empty())
      return false;
    const Envelope &s = slots_[head_ & mask_];
    std::memcpy(static_cast<void *>(&out), &s, sizeof(Envelope));
    ++head_;
    if (++unpublished_ >= batch_)
      publish();
    return true;
  }

  Envelope read() noexcept
  {
    Envelope e;
    while (!read_nb(e))
      cpu_relax();
    return e;
  }
};

} // namespace kfpga
