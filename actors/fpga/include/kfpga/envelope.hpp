#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The one message format used on the card's FIFOs, across PCIe, and by the
 * CPU bridge: a 64-byte envelope, one cache line.
 *
 *   12-byte header   msg id, source actor id, destination actor id
 *   48-byte payload  the message's fields, integers only
 *    4-byte seq      written last; the reader takes the slot once it sees it
 *
 * A message is declared once, in a header compiled into both the FPGA design
 * and the CPU program:
 *
 *   struct Ping : kfpga::Message<301>
 *   {
 *     uint32_t count = 0;
 *     KFPGA_FIELDS(count)
 *   };
 *
 * Kept to C++14, the dialect Vitis HLS accepts.
 */

#include <cstdint>
#include <type_traits>

namespace kfpga {

using ActorId = uint32_t;
using MsgId = uint32_t;

constexpr int kPayloadWords = 12;

struct alignas(64) Envelope
{
  MsgId msg = 0;      // header
  ActorId src = 0;
  ActorId dst = 0;
#ifdef KFPGA_HLS
  uint32_t payload[kPayloadWords];
#else
  uint32_t payload[kPayloadWords] = {};
#endif
  uint32_t seq = 0;   // written last

#ifdef KFPGA_HLS
  // On the chip, zero the payload with an unrolled loop: constant wires, no
  // cycles. The plain `= {}` becomes a loop, one word a cycle (13 cycles on
  // every envelope made). Leaving it unset is not an option either: an
  // uninitialized envelope is X in RTL simulation, and that hung
  // co-simulation.
  Envelope()
  {
    for (int i = 0; i < kPayloadWords; ++i)
    {
#pragma HLS UNROLL
      payload[i] = 0;
    }
  }
#endif
};
static_assert(sizeof(Envelope) == 64, "an envelope is one cache line");

// Base of every message: a fixed, hand-assigned id, the same on both sides.
template <MsgId Id>
struct Message
{
  static constexpr MsgId id = Id;
};

// Lists a message's fields, in order. Each is an integer or enum of 1 to 8
// bytes; prices go in ticks, instruments as indices.
#define KFPGA_FIELDS(...)                                                       \
  template <class F> void kfpga_fields(F &&f) { f(__VA_ARGS__); }               \
  template <class F> void kfpga_fields(F &&f) const { f(__VA_ARGS__); }

namespace detail {

template <class T>
constexpr int words()
{
  static_assert(std::is_integral<T>::value || std::is_enum<T>::value,
                "message fields are integers only");
  static_assert(sizeof(T) <= 8, "message fields are at most 8 bytes");
  return sizeof(T) <= 4 ? 1 : 2;
}

template <class... T> struct total_words;
template <> struct total_words<> { static constexpr int value = 0; };
template <class T, class... R> struct total_words<T, R...>
{
  static constexpr int value = words<T>() + total_words<R...>::value;
};

inline void put(uint32_t *, int) {}
template <class T, class... R>
void put(uint32_t *p, int w, const T &v, const R &...rest)
{
#pragma HLS INLINE
  const uint64_t u = static_cast<uint64_t>(v);
  p[w] = static_cast<uint32_t>(u);
  if (words<T>() == 2)
    p[w + 1] = static_cast<uint32_t>(u >> 32);
  put(p, w + words<T>(), rest...);
}

inline void get(const uint32_t *, int) {}
template <class T, class... R>
void get(const uint32_t *p, int w, T &v, R &...rest)
{
#pragma HLS INLINE
  uint64_t u = p[w];
  if (words<T>() == 2)
    u |= static_cast<uint64_t>(p[w + 1]) << 32;
  v = static_cast<T>(u);
  get(p, w + words<T>(), rest...);
}

} // namespace detail

// The fields of `m` into an envelope's payload, and back.
template <class M>
void put_fields(const M &m, Envelope &e)
{
#pragma HLS INLINE
  m.kfpga_fields([&e](const auto &...v) {
    static_assert(detail::total_words<std::decay_t<decltype(v)>...>::value <= kPayloadWords,
                  "message does not fit the 48-byte payload");
    detail::put(e.payload, 0, v...);
  });
}

template <class M>
void get_fields(const Envelope &e, M &m)
{
#pragma HLS INLINE
  m.kfpga_fields([&e](auto &...v) { detail::get(e.payload, 0, v...); });
}

template <class M>
Envelope encode(const M &m, ActorId src, ActorId dst)
{
#pragma HLS INLINE
  Envelope e;
  e.msg = M::id;
  e.src = src;
  e.dst = dst;
  put_fields(m, e);
  return e;
}

template <class M>
M decode(const Envelope &e)
{
#pragma HLS INLINE
  M m;
  get_fields(e, m);
  return m;
}

} // namespace kfpga
