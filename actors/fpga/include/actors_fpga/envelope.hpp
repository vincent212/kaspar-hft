#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The envelope: one message, as it travels through FIFOs and the
 * host link. The same layout crosses PCIe to and from the CPU.
 *
 *   dst   destination actor id            (0 = the host, i.e. the CPU side)
 *   src   sender actor id                  (replies go back to it)
 *   id    message type id                  (1-15 framework, 300-399 application)
 *   kind  SEND, FAST (a fast_send request) or FAST_REPLY
 *   n     payload words used
 *   w     payload: the message's fields, see KFPGA_FIELDS
 *
 * Messages are plain structs with a static id and a KFPGA_FIELDS list. Fields are
 * integers, enums, bool or char of 8 bytes or less: no pointers, no floating
 * point, so the encoding is exact on the FPGA and on the CPU.
 */

#include <cstdint>
#include <type_traits>

namespace kfpga {

using ActorId = uint16_t;
using MsgId = uint16_t;

constexpr ActorId kHost = 0;        // the CPU side, reached through the host link
constexpr int kMaxActors = 16;      // actor ids 0..15 on one FPGA
constexpr int kPayloadWords = 12;

enum Kind : uint8_t
{
  SEND = 0,
  FAST = 1,        // fast_send request: served before the mailbox, reply returned to the caller
  FAST_REPLY = 2   // answer to a FAST request (id 0 = the handler did not reply)
};

struct Envelope
{
  ActorId dst;
  ActorId src;
  MsgId id;
  uint8_t kind;
  uint8_t n;
  uint32_t w[kPayloadWords];
};

// ---- framework messages ------------------------------------------------------

// Sent to the host when the runtime cannot do what it was asked. Nothing is
// dropped silently.
enum ErrorCode : uint32_t
{
  ERR_NO_HANDLER = 1,     // actor has no handler for message `msg`
  ERR_NO_ROUTE = 2,       // destination actor id not on this FPGA
  ERR_PAYLOAD_FULL = 4,   // a message needs more than kPayloadWords words
  ERR_NO_CODEC = 5,       // the CPU side has no class registered for message `msg`
  ERR_WRONG_REPLY = 6,    // a fast_send was answered with message `msg`, not the type expected
  ERR_CYCLE = 7,          // a fast_send to the calling actor itself would wait on itself
  ERR_SELF_FULL = 8       // an actor's queue of messages to itself is full
};

// ---- field encoding ------------------------------------------------------------

template <class T>
struct field_ok
{
  static constexpr bool value =
      (std::is_integral<T>::value || std::is_enum<T>::value) && sizeof(T) <= 8;
};

struct Writer
{
  uint32_t *w;
  int n;

  template <class T>
  void put(const T &v)
  {
    static_assert(field_ok<T>::value,
                  "FPGA message fields: integers, enums, bool or char of <= 8 bytes");
    const uint64_t x = static_cast<uint64_t>(v);
    if (n < kPayloadWords)
      w[n] = static_cast<uint32_t>(x);
    ++n;
    if (sizeof(T) > 4)
    {
      if (n < kPayloadWords)
        w[n] = static_cast<uint32_t>(x >> 32);
      ++n;
    }
  }
  void operator()() {}
  template <class T, class... R>
  void operator()(const T &f, const R &...r)
  {
    put(f);
    (*this)(r...);
  }
};

struct Reader
{
  const uint32_t *w;
  int n;

  template <class T>
  void get(T &v)
  {
    static_assert(field_ok<T>::value,
                  "FPGA message fields: integers, enums, bool or char of <= 8 bytes");
    uint64_t x = n < kPayloadWords ? w[n] : 0u;
    ++n;
    if (sizeof(T) > 4)
    {
      if (n < kPayloadWords)
        x |= static_cast<uint64_t>(w[n]) << 32;
      ++n;
    }
    v = static_cast<T>(x);
  }
  void operator()() {}
  template <class T, class... R>
  void operator()(T &f, R &...r)
  {
    get(f);
    (*this)(r...);
  }
};

// Build an envelope. Returns false if the message does not fit.
template <class M>
bool pack(const M &m, ActorId dst, ActorId src, uint8_t kind, Envelope &e)
{
  e.dst = dst;
  e.src = src;
  e.id = M::id;
  e.kind = kind;
  for (int i = 0; i < kPayloadWords; ++i)
    e.w[i] = 0;
  Writer wr{e.w, 0};
  m.kfpga_fields(wr);
  e.n = static_cast<uint8_t>(wr.n <= kPayloadWords ? wr.n : kPayloadWords);
  return wr.n <= kPayloadWords;
}

template <class M>
void unpack(const Envelope &e, M &m)
{
  Reader rd{e.w, 0};
  m.kfpga_fields(rd);
}

struct Error
{
  static constexpr MsgId id = 1;
  uint32_t code = 0;
  uint32_t actor = 0;   // actor that raised it
  uint32_t msg = 0;     // message id involved
  uint32_t dst = 0;     // destination involved (ERR_NO_ROUTE)
  template <class F> void kfpga_fields(F &f) { f(code, actor, msg, dst); }
  template <class F> void kfpga_fields(F &f) const { f(code, actor, msg, dst); }
};

inline Envelope make_error(uint32_t code, ActorId actor, MsgId msg, ActorId dst)
{
  Error err;
  err.code = code;
  err.actor = actor;
  err.msg = msg;
  err.dst = dst;
  Envelope e;
  pack(err, kHost, actor, SEND, e);
  return e;
}

// The answer to a fast_send request `req` that failed: an Error, returned to the
// waiting caller as its reply so it is released.
inline Envelope make_fast_reply_error(const Envelope &req, uint32_t code)
{
  Envelope err = make_error(code, req.dst, req.id, req.dst);
  err.kind = FAST_REPLY;
  err.dst = req.src;
  err.src = req.dst;
  return err;
}

// The answer to a fast_send request `req` whose handler did not reply (id 0).
inline Envelope make_no_reply(const Envelope &req)
{
  Envelope none = {};
  none.dst = req.src;
  none.src = req.dst;
  none.id = 0;
  none.kind = FAST_REPLY;
  return none;
}

} // namespace kfpga

// Inside a message: list the fields that travel, e.g.
//   struct Ping { static constexpr kfpga::MsgId id = 300; uint32_t count = 0; KFPGA_FIELDS(count) };
// (The visitor parameter has a reserved-looking name so it cannot collide with a
// field name.)
#define KFPGA_FIELDS(...)                                                        \
  template <class KfpgaF_> void kfpga_fields(KfpgaF_ &kfpga_f_) { kfpga_f_(__VA_ARGS__); } \
  template <class KfpgaF_> void kfpga_fields(KfpgaF_ &kfpga_f_) const { kfpga_f_(__VA_ARGS__); }
