# Dual-Path Decode Verification Plan (parallel vs serial)

## Goal

Run the **same MDP3 packet stream through two independent decode pipelines** —
one parallel (DecodeWorker fleet + Reconstructor), one serial (inline
`mbo_data`) — each feeding its **own** book set. Eventually we compare the two
message streams to prove the parallel decode is equivalent to the serial decode.

This is a differential test harness, not a production feature.

## Scope / phasing

- **Phase 1 (this PR): get the two paths working.** Two decode pipelines, each
  into its own book set, tee'd off the same sequenced packet stream, running
  **live** (we have no PCAPs here). No recorder, no comparison yet — the
  deliverable is both paths running stably side by side.
- **Phase 2 (later): the comparison.** A lightweight recorder on each path's
  books + a diff/checker. Deliberately out of scope for now.

## Background: the serial path still exists

The pre-parallelization serial decode is fully intact in `handler_if`:
- inline `DataDecoder::mbo_data(...)` decodes book/trade,
- maintains its own `orderid_to_securityid` map (`handler_if.hpp:750/900/956`),
- routes to `mbo_order_books` via `BOOKSEND`.

The **only** thing stopping a serial hot-decode today is one assert in
`DataDecoder::on_decode_packet`:

```cpp
if (!sr.corrupt)
    ASSERTF(!sr.has_hot,
        "mixed hot+cold packet: book/trade on the inline path splits the "
        "orderid map -- parallel-decode assumption violated");
```

It exists to catch a mixed hot+cold packet **while parallel is ON** (book/trade
must not take the inline path when the Reconstructor owns the orderid map). But
it also fires on any hot packet when `parallel_decode_` is off, so serial hot
decode crashes on the first book packet. That assert is the only blocker; the
routing underneath works.

## Architecture — tee after sequencing, two independent pipelines

```
SocketReader(s) -> MessageProcessor (msg_q: sequence / gap / dedup -- SHARED)
                        |  fast_send(DecodePacket)  -- same packet to both --+
              +---------+----------+                                         |
              v                    v
     DataDecoder_S (serial)   DataDecoder_P (parallel)
     workers = 0              N workers + Reconstructor_P
     cb = handler_if_S        cb = handler_if_P, .reconstructor = Reconstructor_P
              |                    |
     mbo_order_books_S       mbo_order_books_P
     (its own book set)      (its own book set)
```

The **tee point is `DecodePacket`** — after MessageProcessor's sequencing, so the
gap/dedup/recovery logic stays single and shared, and both pipelines receive
**identical, in-order** input. Each pipeline has its own `handler_if` and its own
book set; they never share state.

## Phase 1 implementation pieces

### 1. Unblock serial hot-decode (prerequisite)

Gate the assert on parallel mode:

```cpp
if (parallel_decode_ && !sr.corrupt)
    ASSERTF(!sr.has_hot, ...);
```

- Parallel ON  -> assert still guards the mixed-packet invariant (unchanged).
- Parallel OFF -> hot packets fall through to inline `mbo_data` -> `handler_if`
  (own orderid map + `BOOKSEND`) -> books. Pure serial. Bonus: the inline path
  returns the real `rc` / `is_channel_reset` synchronously.

### 2. MessageProcessor tees to two decoders

Add an optional shadow decoder: `actor_ptr decoder_shadow_ = nullptr;`. In
`processq`, `fast_send(DecodePacket)` to the primary (use its `DecodeResult`
reply to drive recovery), and if `decoder_shadow_` is set, `fast_send` the same
packet to it (ignore its reply). One knob decides which is primary.

### 3. Two pipelines in `kaspr.cpp` (gated by `cme_verify_parallel = true`)

Build both:
- **Serial:** `handler_if_S` + `DataDecoder_S(workers = 0)` -> its own book set,
  `handler_if_S.reconstructor = nullptr`.
- **Parallel:** `handler_if_P` + `Reconstructor_P` + workers +
  `DataDecoder_P(workers = N)` -> its own book set,
  `handler_if_P.reconstructor = Reconstructor_P`.

Wire both into MessageProcessor (primary + shadow). Manage all the new actors.

## Vehicle: live first

We have no PCAPs here, so Phase 1 runs **live**. Known caveat to handle later: on
a live gap, recovery re-fetches and replays; if the two pipelines ever recover
independently they can diverge. Phase 1 just needs both paths running; making the
recovery path feed both pipelines identically (or quiescing comparison during
recovery) is a Phase 2 concern. A clean-session live run (no gaps) is the first
target.

## Phase 1 verification

- Build `mdp3` + `kaspr` with `cme_verify_parallel = true`.
- Both pipelines start, subscribe, and build their own books from the live feed
  without crashing (the gated assert lets the serial path decode hot packets).
- Sanity-check that both book sets populate (e.g. top-of-book on each path looks
  live and sane). Formal stream comparison is Phase 2.

## Phase 2 (deferred, not this PR)

- Lightweight recorder on each path's books (append the delivered l3 stream:
  seqnum, action, orderID, side, px, qty, securityID).
- Comparison: offline file diff, or an online per-seqnum `Comparator` actor.
- Handle recovery so both pipelines see identical input during a gap.

## Open decisions

1. **Primary path** (feeds the real strategy + drives recovery): parallel
   (production) with serial as the shadow, or the reverse?
2. **Book type for the shadow path** in Phase 1: reuse the real book
   (OB/TachBook) so it exercises the full downstream, or a minimal stand-in?
   (No recorder either way in Phase 1.)
