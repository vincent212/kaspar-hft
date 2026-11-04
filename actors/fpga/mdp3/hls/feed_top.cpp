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

static void filter_task(Fifo &in, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static FilterProc proc;
  FilterProc::member<Filter>().batcher = kfpga::ActorRef(0, kBatcher);
  kfpga::Port ports[1] = {{in, out}};
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
void mdp3_feed_tick(WordStream &words, Fifo &decoded, Fifo &kept, Fifo &out)
{
  decoder_task(words, decoded);
  filter_task(decoded, kept);
  batcher_task(kept, out);
}
#else
static void words_in(InStream &from, WordStream &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(to_word(from.read()));
  }
}

static void lines_out(Fifo &from, Fifo &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(from.read());
  }
}

void mdp3_feed_top(InStream &in, Fifo &out, uint32_t n_words, uint32_t n_out)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS DATAFLOW
  hls_thread_local WordStream words("words");
  hls_thread_local Fifo decoded("decoded");
#pragma HLS STREAM variable=decoded depth=64
  hls_thread_local Fifo kept("kept");
#pragma HLS STREAM variable=kept depth=64
  hls_thread_local Fifo lines("lines");
  hls_thread_local hls::task decoder(decoder_task, words, decoded);
  hls_thread_local hls::task filter(filter_task, decoded, kept);
  hls_thread_local hls::task batcher(batcher_task, kept, lines);
  words_in(in, words, n_words);
  lines_out(lines, out, n_out);
}
#endif
