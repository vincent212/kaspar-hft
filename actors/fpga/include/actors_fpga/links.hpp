#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * Links: the FIFOs that join the endpoints of a design, and the table that says
 * which one leads to a given actor.
 *
 * A design has NE endpoints: its actor processes, 0..NE-2, and the host link,
 * NE-1. Every ordered pair of endpoints (s, r) has three FIFOs, each with one
 * writer (s) and one reader (r), as HLS dataflow requires:
 *
 *   msg[s][r]    messages (send, and replies to a send)
 *   freq[s][r]   fast_send requests from s to r
 *   frep[s][r]   replies from s to a fast_send that r made
 *
 * A sender writes straight into the FIFO that leads to its receiver: there is no
 * stage in between. The discovery table only maps an actor id to the
 * endpoint that holds it, and is fixed when the design is built.
 *
 * Order: each FIFO keeps its order, so messages from one sender to one receiver
 * arrive in the order they were sent.
 *
 * The host link has two inputs: from_host (messages and fast_send requests) and
 * from_host_reply (replies to FPGA actors' fast_sends to the CPU). A reply never
 * waits behind a message for an actor whose FIFO is full, so an actor waiting on
 * the CPU always gets its answer.
 */

#include "actors_fpga/envelope.hpp"
#include "actors_fpga/stream.hpp"

namespace kfpga {

constexpr int8_t kToHost = -1;
constexpr int8_t kNoRoute = -2;

struct DiscoveryTable
{
  int8_t port[kMaxActors];  // actor id -> endpoint (actor process index), kToHost or kNoRoute
};

inline int8_t lookup(const DiscoveryTable &rt, ActorId dst)
{
  if (dst >= kMaxActors)
    return kNoRoute;
  return rt.port[dst];
}

// The endpoint for actor `dst` in a design with NE endpoints, or -1 if none.
template <int NE>
int endpoint(const DiscoveryTable &rt, ActorId dst)
{
  const int8_t p = lookup(rt, dst);
  if (p == kToHost)
    return NE - 1;
  if (p >= 0 && p < NE - 1)
    return p;
  return -1;
}

template <int NE>
struct Links
{
  static constexpr int kHostEp = NE - 1;
  hls::stream<Envelope> msg[NE][NE];
  hls::stream<Envelope> freq[NE][NE];
  hls::stream<Envelope> frep[NE][NE];
  hls::stream<Envelope> reply_err;   // from host_in_reply to host_out
};

// Endpoint K writes to endpoint `to`. Unrolled over constant indices so Vitis HLS
// builds fixed connections instead of a runtime-indexed stream.
template <int NE, int K>
void write_to(hls::stream<Envelope> (&a)[NE][NE], int to, const Envelope &e)
{
  for (int j = 0; j < NE; ++j)
  {
#pragma HLS UNROLL
    if (j == to && j != K)
      a[K][j].write(e);
  }
}

// Endpoint K reads from endpoint `from` (blocking).
template <int NE, int K>
Envelope read_from(hls::stream<Envelope> (&a)[NE][NE], int from)
{
  Envelope e = {};
  for (int j = 0; j < NE; ++j)
  {
#pragma HLS UNROLL
    if (j == from && j != K)
      e = a[j][K].read();
  }
  return e;
}

// The next endpoint with something waiting for K, round-robin after `last`, or -1.
template <int NE, int K>
int pick_from(hls::stream<Envelope> (&a)[NE][NE], int last)
{
  int chosen = -1;
  int best = NE;
  for (int j = 0; j < NE; ++j)
  {
#pragma HLS UNROLL
    const int rank = (j - last - 1 + 2 * NE) % NE;   // 0 = next in turn
    if (j != K && !a[j][K].empty() && rank < best)
    {
      best = rank;
      chosen = j;
    }
  }
  return chosen;
}

// Host -> FPGA, messages and fast_send requests: each envelope goes straight into
// the FIFO from the host to its actor. What cannot be delivered is answered on
// msg[host][host], which the host output sends back out.
template <int NE>
bool host_in_step(hls::stream<Envelope> &from_host, Links<NE> &L, const DiscoveryTable &rt)
{
  constexpr int H = NE - 1;
  if (from_host.empty())
    return false;
  const Envelope e = from_host.read();
  const int8_t p = lookup(rt, e.dst);
  if (p >= 0 && p < H && e.kind != FAST_REPLY)
  {
    if (e.kind == FAST)
      write_to<NE, H>(L.freq, p, e);
    else
      write_to<NE, H>(L.msg, p, e);
  }
  else if (e.kind == FAST)
  {
    // The caller is waiting: answer it with an error instead of leaving it hanging.
    L.msg[H][H].write(make_fast_reply_error(e, ERR_NO_ROUTE));
  }
  else
  {
    L.msg[H][H].write(make_error(ERR_NO_ROUTE, kHost, e.id, e.dst));
  }
  return true;
}

// Host -> FPGA, replies: each goes into the reply FIFO from the host to the actor
// that is waiting for it. An actor has at most one fast_send outstanding, so that
// FIFO never holds more than one reply and this process never blocks.
template <int NE>
bool host_in_reply_step(hls::stream<Envelope> &from_host_reply, Links<NE> &L,
                        const DiscoveryTable &rt)
{
  constexpr int H = NE - 1;
  if (from_host_reply.empty())
    return false;
  const Envelope e = from_host_reply.read();
  const int8_t p = lookup(rt, e.dst);
  if (p >= 0 && p < H && e.kind == FAST_REPLY)
    write_to<NE, H>(L.frep, p, e);
  else
    L.reply_err.write(make_error(ERR_NO_ROUTE, kHost, e.id, e.dst));
  return true;
}

// FPGA -> host: merge everything addressed to the host onto the one PCIe stream,
// round-robin. Inputs, numbered for the arbiter: 0..NE-1 msg[s][H],
// NE..2NE-2 freq[s][H], 2NE-1..3NE-3 frep[s][H], 3NE-2 reply_err.
template <int NE>
bool host_out_step(Links<NE> &L, hls::stream<Envelope> &to_pcie, int &last)
{
  constexpr int H = NE - 1;
  constexpr int N = NE + 2 * (NE - 1) + 1;
  int chosen = -1;
  int best = N;
  for (int j = 0; j < N; ++j)
  {
#pragma HLS UNROLL
    bool ready;
    if (j < NE)
      ready = !L.msg[j][H].empty();
    else if (j < 2 * NE - 1)
      ready = !L.freq[j - NE][H].empty();
    else if (j < N - 1)
      ready = !L.frep[j - (2 * NE - 1)][H].empty();
    else
      ready = !L.reply_err.empty();
    const int rank = (j - last - 1 + 2 * N) % N;
    if (ready && rank < best)
    {
      best = rank;
      chosen = j;
    }
  }
  if (chosen < 0)
    return false;
  for (int j = 0; j < N; ++j)
  {
#pragma HLS UNROLL
    if (j != chosen)
      continue;
    if (j < NE)
      to_pcie.write(L.msg[j][H].read());
    else if (j < 2 * NE - 1)
      to_pcie.write(L.freq[j - NE][H].read());
    else if (j < N - 1)
      to_pcie.write(L.frep[j - (2 * NE - 1)][H].read());
    else
      to_pcie.write(L.reply_err.read());
  }
  last = chosen;
  return true;
}

} // namespace kfpga
