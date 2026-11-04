/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The MDP3 handler as hardware: decoder, filter and batcher, three actors in
// three processes. The top moves n_words words and n_out envelopes around the
// free-running tasks: the counted boundary of HLS.md section 3.

#include <hls_stream.h>
#include <hls_task.h>

#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;
using kfpga::Fifo;

using DecoderProc = kfpga::Process<SbeDecoder>;
using FilterProc = kfpga::Process<Filter>;
using BatcherProc = kfpga::Process<Batcher>;

static void decoder_task(WordStream &in, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static DecoderProc proc;
  DecoderProc::member<SbeDecoder>().books = kfpga::ActorRef(0, kFilter);
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
  b.feed = kfpga::ActorRef(0, kFeed);
#ifdef MDP3_SW
  static uint32_t clock = 0;
  b.now = ++clock;
#endif
  kfpga::Port ports[1] = {{in, out}};
  proc.step(ports);
}

#ifdef MDP3_SW
// Plain g++, for whole recordings: one clock of the three processes.
void mdp3_feed_tick(WordStream &words, Fifo &decoded, Fifo &kept, Fifo &window, Fifo &acks, Fifo &out)
{
  decoder_task(words, decoded);
  filter_task(decoded, kept, window, acks);
  batcher_task(kept, out);
}
#else
// The PCIe side, in counts as in HLS.md section 3. As the CPU does, load the table
// first and let packets in only when every entry is acked.
static void load(Fifo &from, Fifo &window, Fifo &acks, Fifo &acked, hls::stream<bool> &go, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
    window.write(from.read());
    acked.write(acks.read());
  }
  go.write(true);
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

// `window` the CPU's table entries, n_want of them; `out` their acks, then n_out lines.
void mdp3_feed_top(InStream &in, Fifo &window, Fifo &out, uint32_t n_words, uint32_t n_want, uint32_t n_out)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=window
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS DATAFLOW
  hls_thread_local WordStream words("words");
  hls_thread_local Fifo decoded("decoded");
#pragma HLS STREAM variable=decoded depth=64
  hls_thread_local Fifo kept("kept");
#pragma HLS STREAM variable=kept depth=64
  hls_thread_local Fifo lines("lines");
  hls_thread_local Fifo to_filter("to_filter");
  hls_thread_local Fifo acks("acks");
  hls_thread_local hls::task decoder(decoder_task, words, decoded);
  hls_thread_local hls::task filter(filter_task, decoded, kept, to_filter, acks);
  hls_thread_local hls::task batcher(batcher_task, kept, lines);
  Fifo acked("acked");
#pragma HLS STREAM variable=acked depth=64
  hls::stream<bool> go("go");
  load(window, to_filter, acks, acked, go, n_want);
  words_in(in, words, go, n_words);
  lines_out(acked, lines, out, n_want, n_out);
}
#endif
