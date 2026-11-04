/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Decoder and book as hardware (phase 2 of MDP3_PLAN.md): two actors, each its
// own process, one link between them. The top moves n_words words and n_out
// envelopes around the free-running tasks: the counted boundary of HLS.md
// section 3.

#include <hls_stream.h>
#include <hls_task.h>

#include "mdp3fpga/book.hpp"
#include "mdp3fpga/decoder.hpp"

using namespace mdp3fpga;
using kfpga::Fifo;

using DecoderProc = kfpga::Process<SbeDecoder>;
using BookProc = kfpga::Process<BookBank>;

#ifndef MDP3_SECURITY
#define MDP3_SECURITY 203686713
#endif

static void decoder_task(WordStream &in, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static DecoderProc proc;
  DecoderProc::member<SbeDecoder>().books = kfpga::ActorRef(0, kBooks);
  kfpga::OutPort ports[1] = {{out}};
  proc.step_stream<SbeDecoder>(ports, in);
}

static void book_task(Fifo &in, Fifo &held_in, Fifo &held_out, Fifo &out)
{
#pragma HLS PIPELINE II=1
  static BookProc proc;
  BookBank &b = BookProc::member<BookBank>();
  b.feed = kfpga::ActorRef(0, kFeed);
  b.security = MDP3_SECURITY;
  kfpga::OutPort ports[1] = {{out}};
  proc.step_stream<BookBank>(ports, in, held_in, held_out);
}

#ifdef MDP3_SW
// Plain g++, for whole recordings: one clock of both processes.
void mdp3_tick(WordStream &words, Fifo &link, Fifo &out)
{
  static Fifo held;
  decoder_task(words, link);
  book_task(link, held, held, out);
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

static void batches_out(Fifo &from, Fifo &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
#pragma HLS PIPELINE II=1
    to.write(from.read());
  }
}

void mdp3_book_top(InStream &in, Fifo &out, uint32_t n_words, uint32_t n_out)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS DATAFLOW
  hls_thread_local WordStream words("words");
  hls_thread_local Fifo link("link");
#pragma HLS STREAM variable=link depth=128
  hls_thread_local Fifo held("held");
#pragma HLS STREAM variable=held depth=64
  hls_thread_local Fifo batches("batches");
  hls_thread_local hls::task decoder(decoder_task, words, link);
  hls_thread_local hls::task book(book_task, link, held, held, batches);
  words_in(in, words, n_words);
  batches_out(batches, out, n_out);
}
#endif
