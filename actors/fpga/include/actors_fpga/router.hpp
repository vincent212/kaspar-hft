#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The router and the host link: how a message finds its actor.
 *
 *   actors' to_router ─┐
 *                      ├─> router ──> actor mailboxes
 *   host_in ───────────┘        └──> to_host (dst = host, errors)
 *
 *   from_host ──> host_in_step ──> host_in (SEND) / actor fast_in (FAST) /
 *                                  actor call_reply (FAST_REPLY)
 *   to_host + host_err + actors' fast_reply ──> host_out_step ──> PCIe
 *
 * A route table maps an actor id to its port (its index in the mailbox array),
 * or to kToHost. It is fixed when the design is built.
 *
 * Order: an actor's output FIFO is read in order, so messages from one sender to
 * one receiver arrive in the order they were sent.
 */

#include "actors_fpga/envelope.hpp"
#include "actors_fpga/stream.hpp"

namespace kfpga {

constexpr int8_t kToHost = -1;
constexpr int8_t kNoRoute = -2;

struct RouteTable
{
  int8_t port[kMaxActors];  // actor id -> mailbox index, kToHost or kNoRoute
};

inline int8_t lookup(const RouteTable &rt, ActorId dst)
{
  if (dst >= kMaxActors)
    return kNoRoute;
  return rt.port[dst];
}

// Pick the next non-empty input in round-robin order, starting after `last`.
// Written as unrolled loops over constant indices so Vitis HLS builds a fixed
// arbiter instead of a runtime-indexed stream. Returns -1 if all are empty.
template <int N>
int pick(hls::stream<Envelope> (&in)[N], int last)
{
  int chosen = -1;
  int best = N;
  for (int j = 0; j < N; ++j)
  {
#pragma HLS UNROLL
    const int rank = (j - last - 1 + 2 * N) % N;   // 0 = next in turn
    if (!in[j].empty() && rank < best)
    {
      best = rank;
      chosen = j;
    }
  }
  return chosen;
}

template <int N>
Envelope read_at(hls::stream<Envelope> (&in)[N], int idx)
{
  Envelope e = {};
  for (int j = 0; j < N; ++j)
  {
#pragma HLS UNROLL
    if (j == idx)
      e = in[j].read();
  }
  return e;
}

template <int N>
void write_at(hls::stream<Envelope> (&out)[N], int idx, const Envelope &e)
{
  for (int j = 0; j < N; ++j)
  {
#pragma HLS UNROLL
    if (j == idx)
      out[j].write(e);
  }
}

// One step: take at most one message from the inputs, in round-robin order
// starting after the input served last, and deliver it. Returns false if all
// inputs were empty.
template <int NIn, int NAct>
bool router_step(hls::stream<Envelope> (&in)[NIn], hls::stream<Envelope> (&mbox)[NAct],
                 hls::stream<Envelope> &to_host, const RouteTable &rt, int &last)
{
  const int i = pick(in, last);
  if (i < 0)
    return false;
  const Envelope e = read_at(in, i);
  last = i;
  // A remote fast_send request always goes to the host: to a CPU actor it is
  // served there; to an FPGA actor in another process the host relays it back
  // to that actor's fast_send port and relays the reply to the caller.
  if (e.kind == FAST)
  {
    to_host.write(e);
    return true;
  }
  const int8_t p = lookup(rt, e.dst);
  if (p == kToHost)
    to_host.write(e);
  else if (p >= 0 && p < NAct)
    write_at(mbox, p, e);
  else
    to_host.write(make_error(ERR_NO_ROUTE, e.src, e.id, e.dst));
  return true;
}

// Host -> FPGA:
//   SEND        into the router, like any message
//   FAST        a fast_send from the host: straight to the destination actor's
//               fast_send port, past its mailbox
//   FAST_REPLY  the answer to an FPGA actor's remote fast_send: to that actor's
//               call_reply port, where its process is waiting
template <int NAct>
bool host_in_step(hls::stream<Envelope> &from_host, hls::stream<Envelope> &host_in,
                  hls::stream<Envelope> (&fast_in)[NAct],
                  hls::stream<Envelope> (&call_reply)[NAct], hls::stream<Envelope> &host_err,
                  const RouteTable &rt)
{
  if (from_host.empty())
    return false;
  const Envelope e = from_host.read();
  if (e.kind == SEND)
  {
    host_in.write(e);
    return true;
  }
  const int8_t p = lookup(rt, e.dst);
  if (p >= 0 && p < NAct)
  {
    if (e.kind == FAST)
      write_at(fast_in, p, e);
    else
      write_at(call_reply, p, e);
  }
  else if (e.kind == FAST)
  {
    // The caller is waiting: answer with an error instead of leaving it hanging.
    Envelope err = make_error(ERR_NO_ROUTE, kHost, e.id, e.dst);
    err.kind = FAST_REPLY;
    err.dst = e.src;
    err.src = e.dst;
    host_err.write(err);
  }
  else
  {
    host_err.write(make_error(ERR_NO_ROUTE, kHost, e.id, e.dst));
  }
  return true;
}

// FPGA -> host: merge messages addressed to the host, host-link errors and
// fast_send replies. Arbiter inputs: 0..NAct-1 fast replies, NAct to_host,
// NAct+1 host_err. Every stream has one writer, as HLS dataflow requires.
template <int NAct>
bool host_out_step(hls::stream<Envelope> &to_host, hls::stream<Envelope> &host_err,
                   hls::stream<Envelope> (&fast_reply)[NAct], hls::stream<Envelope> &to_pcie,
                   int &last)
{
  constexpr int N = NAct + 2;
  int chosen = -1;
  int best = N;
  for (int j = 0; j < N; ++j)
  {
#pragma HLS UNROLL
    bool ready;
    if (j == NAct)
      ready = !to_host.empty();
    else if (j == NAct + 1)
      ready = !host_err.empty();
    else
      ready = !fast_reply[j < NAct ? j : 0].empty();
    const int rank = (j - last - 1 + 2 * N) % N;
    if (ready && rank < best)
    {
      best = rank;
      chosen = j;
    }
  }
  if (chosen < 0)
    return false;
  if (chosen == NAct)
    to_pcie.write(to_host.read());
  else if (chosen == NAct + 1)
    to_pcie.write(host_err.read());
  else
    to_pcie.write(read_at(fast_reply, chosen));
  last = chosen;
  return true;
}

} // namespace kfpga
