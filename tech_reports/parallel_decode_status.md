# Parallel decode: where it actually stands

**SHELVED 2026-09-24 by decision.** The effort ran well over its estimate
and is stopped here, not paused on a next step. This document exists so the
next person does not re-derive it: what is on main, what is missing, what
was measured, and what was wasted.

Serial remains the default on main (4b6d889), so nothing needs to be
reverted and nothing in production is affected.

The reasons are kept separate below into "the problem is harder than it
looked" and "we wasted time", because only the first is an argument about
whether to ever restart.

## What is on main

`main` (4b6d889) ships the parallel decoder, **with serial as the default
and parallel behind a switch**:

| commit  | subject |
|---------|---------|
| 2f4a486 | parallelize MBO decode across a warm DecodeWorker fleet |
| fc28147 | fix parallel-decode handler-state races (asset map, channel reset) |
| 921e554 | recover on a parallel decode failure instead of silently diverging |
| 17f4aea | unit tests for the parallel decode path (Reconstructor + scan) |
| 688f854 06fcaa2 f22c6ca | ingress-qlen stamp pinned across the path |
| 4b6d889 | restore serial as the default, parallel behind a switch (#120) |

Nothing in production is affected: the default path is serial.

## What is missing

Four gaps. The first is disqualifying on its own.

### 1. Mixed hot+cold packets — live-fatal

`DataDecoder.hpp:701` asserts if a packet mixes hot (book tid 46, trade
tid 48) with any other template. With parallel on, that assert fires on
the live feed.

Measured, live ES chan 310, **80,000 packets**:

- **3.03% of packets** are mixed (10.21% of messages)
- the cold template is `MDIncrementalRefreshVolume37` in **1,419 of 1,419**
  mixed packets

So parallel decode on main cannot survive contact with the real feed. The
comment in the source says so in its own words.

**Why it is not just a missing feature.** There are two copies of the
order map:

```
Reconstructor.hpp:173   orderid_to_securityid_    (parallel path)
handler_if.hpp:61       orderid_to_securityid     (inline path)
```

A book `New` writes the orderID -> securityID entry into whichever map its
path owns (`Reconstructor.hpp:119`, `handler_if.hpp:750`). A Trade message
carries **only** the orderID and must resolve securityID from that map. If
the `New` went parallel and the `Trade` falls inline, the lookup misses and
the trade is dropped at `Reconstructor.hpp:132`:

```cpp
if (oit == orderid_to_securityid_.end()) [[unlikely]]
    return; // order not known -> not in universe
```

Silent. No log, no counter. The output streams merge into the same book
actors; the *lookup state* forks. That is the entire hazard.

**The invariant that must hold is: one owner of `orderid_to_securityid`.**
Three designs satisfy it:

- **(a) Packet split** — route per message, hot to the workers, cold
  inline, so hot never takes the inline path. This is PR #123: a three-way
  classifier plus a split walk plus nine new tests.
- **(b) Drop the fallback** — send the mixed packet down the parallel path
  anyway and apply the cold message on the Reconstructor thread. One map,
  no classifier, no split walk. Cost: `DecodeWorker` currently asserts on
  non-hot templates, and Volume37's effect needs a home (see gap 4, which
  (a) does not solve either).
- **(c) Strip the map out of `handler_if`** — have its two MBO write sites
  delegate to the Reconstructor when parallel is on. The inline path
  becomes stateless, mixing becomes free, and the assert can be deleted.

(a) was built without pricing (b) or (c). Given the measured shape of the
problem — 3.03% of packets, one cold template in 1,419 of 1,419 cases —
that is the wrong order of operations. **This decision is open and blocks
#123.**

### 2. EndOfBurst — flagged "required before live"

`Reconstructor.hpp:51`, with two dead sites at `:124` and `:135`:

```cpp
// TODO: if (l3.endOfEvent) emit_burstend();
```

The serial path carries `endOfEvent` through `handler_if.hpp:621/757/829/887`
and emits `BurstEnd` at `:1265`. The parallel path drops it.

This is not a performance gap. It is a **behavioural divergence**:
downstream (lights/SOM) never fires on the parallel path. It also means any
serial-vs-parallel A/B is not comparing equal outputs, so the comparison is
not valid until this lands.

### 3. COLD_ORDERED has no barrier

Even with the splitter, `splittable()` is narrow (`DataDecoder.hpp:197`):

```cpp
return !corrupt && n_hot > 0 && n_cold_ordered == 0 && n_cold_independent > 0;
```

A packet containing a definition, `ChannelReset4`, or `SecurityStatus30`
still falls back to whole-packet inline. That is the correct conservative
default, and it happens to cover the measured case — but the coverage is an
empirical accident of the census, not a guarantee.

### 4. Volume37 / statistics never reach the Reconstructor

Grepping `Reconstructor.hpp` for `Volume37|statistic` returns only comments.
The parallel path has no handling for them at all; they exist solely on the
inline leg. Unsolved under every design above.

## Does it work?

Unit tests pass — 17f4aea added them, 906c228 takes the suite to 396 from
387. So it works against synthetic packets.

It has never survived the live feed. Gap 1 guarantees it cannot, and gap 2
means that even if it ran, its output would differ from serial's.

## Measurement status: nothing valid yet

The dual-path tee (#124) exists to run serial and parallel over the *same*
packets and diff them. A 5-minute window was run at 14:25-14:30. **Those
numbers are not usable**, for reasons that are still open:

- the `_P` leg showed a near-constant ~2.0x p50 ratio against `_S`
  (1.90, 1.96, 2.06, 2.07, 2.04, 1.69 across six contract/stream pairs) —
  the signature of `_P = _S + decode`, i.e. the shadow starting only after
  the serial decode finished, despite the source dispatching the shadow
  `send()` *before* the primary `fast_send()` (`MessageProcessor.hpp:439`
  vs `:450`). **Not explained.**
- the shadow books never received the recovery snapshot
  (`VERIFY shadow INVALIDATED` fired 3x at startup), giving the `_P` leg
  roughly 9% fewer records. Not separated from the above.
- gap 2 means the two legs do not emit the same thing anyway.

Two candidate causes were investigated and **eliminated**:

- *"the libs were never rebuilt"* — false. `make -B -n` in `mdp3/src` shows
  `-DMDP3_VERIFY_TEE` on the `message_processor.cpp` compile line, and the
  linked binary contains the tee's string literals.
- *"the define never reached `libmdp3.a`"* — false, same evidence. The test
  that appeared to prove it (`strings message_processor.o`) is invalid:
  the object is an LTO object (`.gnu.lto_*` sections, compressed GIMPLE),
  so source literals are not findable in it. `strings` on an `-flto` object
  proves nothing in either direction.

## Why this has taken longer than expected

Two separate causes. Keeping them apart matters for estimating the rest.

**Genuine scope that was underestimated.** "Decode on N threads" was
costed as a threading change. It is not. It is an ownership change: the
order map has to have exactly one owner, and every path that writes it has
to be routed accordingly. Gap 1 is that, gap 2 is a second output contract
that was never wired, gaps 3 and 4 are the tail of the same thing. None of
these show up in a microbenchmark, and the unit tests pass without them.

**Avoidable losses.** These were mistakes, not discovery:

1. Two builds were launched **concurrently into the same tree with opposite
   `-D` flags** (`VERIFY_TEE=1` and not). One of them also ran with an empty
   `KSPRPROJ` and reported `kaspr rc=0` from `tail`'s exit status rather than
   the compiler's. The resulting binary was linked and run against a live
   5-minute market window. That window is gone.
2. A root cause — "the libs were never rebuilt" — was asserted from
   `strings` on an LTO object, which cannot answer the question. Roughly an
   hour went into a build defect that did not exist, and the false
   conclusion was written into project memory as settled fact.
3. A question about a missing `build.sh` was answered against the wrong
   working tree (`/home/vincent/kaspar-hft`, a bench branch 285 commits
   behind main and dated one day before the commit that added the file),
   and reported as a general fact. The file had been present the whole time
   in the tree actually being built in.
4. 442 lines of tee work sat uncommitted across a context boundary while
   being built and measured, so PR #124 did not contain the code that ran.
5. Design (a) for gap 1 was built before designs (b) and (c) were priced.

Lessons 1 and 2 are now in `CLAUDE.md`. Note that the entry there
attributing the 2.0x result to a stale lib is **wrong** and is corrected by
this document.

## If it is ever restarted

Not a plan — a precondition list. Anyone restarting this should do these in
order, and should not spend a market window before step 3 is answered.

1. **Decide gap 1's design first** — (a) split, (b) drop the fallback, or
   (c) strip the map out of `handler_if`. #123 is only correct under (a),
   and (a) is the most expensive of the three. Price (b) and (c) before
   touching code.
2. **Wire EndOfBurst** (gap 2). Until this lands, no serial-vs-parallel
   comparison is valid, because the two paths do not produce the same
   output. Measuring before this is measuring nothing.
3. **Explain the 2.0x ratio** in the tee, or discard the tee as a
   measurement instrument. It is currently an unexplained result, not a
   working tool.

The honest summary of the payoff side: the decode cost that parallelism
targets has never been shown to be the binding constraint. The standing
hypothesis for this system is that latency is governed by queue depth, not
by software cost off the hot path. Nothing measured here contradicts that,
and the tee — the instrument built to test it — did not produce a usable
number.

## Open PRs at the time of shelving

| PR | branch | state |
|----|--------|-------|
| #123 | `mdp3/parallel-packet-split` | blocked on the gap-1 design decision; close unless (a) is chosen |
| #124 | `mdp3/dual-path-tee` | stacked on #123; measurement never valid |

Both are left open rather than merged. Neither should be merged on the
strength of anything in this document.
