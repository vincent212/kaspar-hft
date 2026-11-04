/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The MDP3 decoder as hardware (phase 1 of MDP3_PLAN.md): the SbeDecoder actor
// in its own process, an hls::task taking one step a clock. UDP payloads in as
// 8-byte AXI-Stream words, events out as envelopes. The top moves n_words words
// and n_events envelopes around the free-running task: the counted boundary of
// HLS.md section 3.

#include <hls_stream.h>
#include <hls_task.h>

#include "mdp3fpga/decoder.hpp"

using namespace mdp3fpga;
using kfpga::Fifo;

using DecoderProc = kfpga::Process<SbeDecoder>;

static void decoder_task(WordStream &in, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static DecoderProc proc;
  SbeDecoder &d = DecoderProc::member<SbeDecoder>();
  d.books = kfpga::ActorRef(0, kBooks);
  kfpga::OutPort ports[1] = {{out}};
  proc.step_stream<SbeDecoder>(ports, in);
}

#ifdef MDP3_SW
// Plain g++, for whole recordings (tb_stream.cpp): one step at a time, without
// the simulated threads of a task.
void mdp3_step(WordStream &words, Fifo &out) { decoder_task(words, out); }
#else
static void words_in(InStream &from, WordStream &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(to_word(from.read()));
  }
}

static void events_out(Fifo &from, Fifo &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(from.read());
  }
}

void mdp3_decoder_top(InStream &in, Fifo &out, uint32_t n_words, uint32_t n_events)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS DATAFLOW
  hls_thread_local WordStream words("words");
  hls_thread_local Fifo events("events");
  hls_thread_local hls::task decoder(decoder_task, words, events);
  words_in(in, words, n_words);
  events_out(events, out, n_events);
}
#endif
