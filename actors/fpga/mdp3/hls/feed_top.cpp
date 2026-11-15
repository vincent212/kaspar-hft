/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The MDP3 handler as hardware: FeedRx, SbeDecoder, Filter, Batcher and Igmp,
// five actors in five processes. The top moves n_words words and n_out envelopes around the
// free-running tasks: the counted boundary of HLS.md section 3.

#include <hls_stream.h>
#include <hls_task.h>

#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"
#include "mdp3fpga/feed_rx.hpp"
#include "mdp3fpga/igmp.hpp"

using namespace mdp3fpga;
using kfpga::Fifo;

using RxProc = kfpga::Process<FeedRx>;
using DecoderProc = kfpga::Process<SbeDecoder>;
using FilterProc = kfpga::Process<Filter>;
using BatcherProc = kfpga::Process<Batcher>;
using IgmpProc = kfpga::Process<Igmp>;

// FeedRx: frames in, packets out; Listen in from Igmp, IgmpQuery out to it (port 0).
static void rx_task(WordStream &frames, WordStream &words, Fifo &listen, Fifo &queries)
{
#pragma HLS PIPELINE II=1
  static RxProc proc;
  RxProc::member<FeedRx>().igmp = kfpga::ActorRef(0, kIgmp);
  kfpga::OutPort ports[1] = {{queries}};
  proc.step_stream<FeedRx>(ports, frames, words, listen);
}

static void decoder_task(WordStream &in, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static DecoderProc proc;
  DecoderProc::member<SbeDecoder>().filter = kfpga::ActorRef(0, kFilter);
  kfpga::OutPort ports[1] = {{out}};
  proc.step_stream<SbeDecoder>(ports, in);
}

// Port 0: decoder in, batcher out. Port 1: the CPU's link.
static void filter_task(Fifo &in, Fifo &out, Fifo &window, Fifo &acks)
{
#pragma HLS PIPELINE II=1
  static FilterProc proc;
  FilterProc::member<Filter>().batcher = kfpga::ActorRef(0, kBatcher);
  kfpga::Port ports[2] = {{in, out}, {window, acks}};
  proc.step(ports);
}

static void batcher_task(Fifo &in, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static BatcherProc proc;
  Batcher &b = BatcherProc::member<Batcher>();
  b.cpu_feed = kfpga::ActorRef(0, kCpuFeed);
  kfpga::Port ports[1] = {{in, out}};
  proc.step(ports);
}

// Igmp. Port 0: the CPU's link (JoinGroup, LeaveGroup; IgmpAck). Port 1: FeedRx
// (IgmpQuery; Listen). `tx` goes to the MAC's transmit side. Each clock it either
// works on a frame (build, write a word, ack) or handles a message.
static void igmp_task(Fifo &join, Fifo &joined, Fifo &queries, Fifo &listen, WordStream &tx)
{
#pragma HLS PIPELINE II=1
  static IgmpProc proc;
  Igmp &igmp = IgmpProc::member<Igmp>();
  igmp.cpu = kfpga::ActorRef(0, kCpuFeed);
  igmp.feed_rx = kfpga::ActorRef(1, kFeedRx);
  kfpga::Port ports[2] = {{join, joined}, {queries, listen}};
  igmp.on_clock();
  if (igmp.busy())
    proc.step_stream<Igmp>(ports, tx, queries);
  else
    proc.step(ports);
}

#ifdef MDP3_SW
// Plain g++, for whole recordings: one clock of the five processes, each task
// called once, in the order of the flow (DESIGN.md).
void mdp3_feed_tick(WordStream &frames, WordStream &words, Fifo &decoded, Fifo &kept, Fifo &window, Fifo &acks,
                    Fifo &out, Fifo &join, Fifo &joined, Fifo &queries, Fifo &listen, WordStream &tx)
{
  rx_task(frames, words, listen, queries);
  decoder_task(words, decoded);
  filter_task(decoded, kept, window, acks);
  batcher_task(kept, out);
  igmp_task(join, joined, queries, listen, tx);
}
#else
// Test harness, not part of the card. load, words_in, lines_out and tx_out are the
// counted boundary that co-simulation needs (HLS.md section 3): they move a known
// number of words and envelopes between the top-level ports and the free-running
// actor tasks, so that the top returns and the testbench can check the result. On
// the card the MAC feeds `frames` and takes `tx`, the DMA engine drains `lines` and
// `joined`, and these functions do not exist. They are plain dataflow functions
// because they only count; no stage of the handler is among them.
//
// As the CPU does, load the table and join the group first, and let packets in only
// when every entry and the join are acked.
static void load(Fifo &from, Fifo &window, Fifo &acks, Fifo &join_from, Fifo &join, Fifo &joined, Fifo &acked,
                 hls::stream<bool> &go, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
    window.write(from.read());
    acked.write(acks.read());
  }
  join.write(join_from.read());
  acked.write(joined.read());
  go.write(true);
}

static void tx_out(WordStream &tx, InStream &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    const Word w = tx.read();
    InWord o;
    o.data = w.data;
    o.keep = w.keep;
    o.last = w.last;
    to.write(o);
  }
}

static void words_in(InStream &from, WordStream &to, hls::stream<bool> &go, uint32_t n)
{
  go.read();
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(to_word(from.read()));
  }
}

static void lines_out(Fifo &acked, Fifo &from, Fifo &to, uint32_t n_acks, uint32_t n)
{
  for (uint32_t i = 0; i < n_acks; ++i)
    to.write(acked.read());
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(from.read());
  }
}

// `in` Ethernet frames, n_words words; `window` the CPU's table entries, n_want of
// them; `join` one JoinGroup; `out` the table's acks and the join's, then n_out
// lines; `tx` the first n_tx words the card transmits.
void mdp3_feed_top(InStream &in, Fifo &window, Fifo &join_in, Fifo &out, InStream &tx_port, uint32_t n_words,
                   uint32_t n_want, uint32_t n_out, uint32_t n_tx)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=window
#pragma HLS INTERFACE mode=axis port=join_in
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS INTERFACE mode=axis port=tx_port
#pragma HLS DATAFLOW
  hls_thread_local WordStream frames("frames");
  hls_thread_local WordStream words("words");
  hls_thread_local Fifo decoded("decoded");
#pragma HLS STREAM variable=decoded depth=64
  hls_thread_local Fifo kept("kept");
#pragma HLS STREAM variable=kept depth=64
  hls_thread_local Fifo lines("lines");
  hls_thread_local Fifo to_filter("to_filter");
  hls_thread_local Fifo acks("acks");
  hls_thread_local Fifo join("join");
  hls_thread_local Fifo joined("joined");
  hls_thread_local Fifo queries("queries");
#pragma HLS STREAM variable=queries depth=4
  hls_thread_local Fifo listen("listen");
  hls_thread_local WordStream tx("tx");
  hls_thread_local hls::task rx(rx_task, frames, words, listen, queries);
  hls_thread_local hls::task decoder(decoder_task, words, decoded);
  hls_thread_local hls::task filter(filter_task, decoded, kept, to_filter, acks);
  hls_thread_local hls::task batcher(batcher_task, kept, lines);
  hls_thread_local hls::task igmp(igmp_task, join, joined, queries, listen, tx);
  Fifo acked("acked");
#pragma HLS STREAM variable=acked depth=65
  hls::stream<bool> go("go");
  load(window, to_filter, acks, join_in, join, joined, acked, go, n_want);
  words_in(in, frames, go, n_words);
  lines_out(acked, lines, out, n_want + 1, n_out);
  tx_out(tx, tx_port, n_tx);
}
#endif
