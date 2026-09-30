# Extended actor model: plan

Working title: *Extending the Actor Model with Synchronous Delivery: fast_send from CPU Threads to FPGA Pipelines*.

## Thesis

The actor model has one primitive, asynchronous send. We add a second one, `fast_send`:
the receiver's turn runs now, on the sender's execution resource, and its reply comes
back as a value. We give it a formal semantics and show that it preserves the actor
model's defining property, isolated turns. We characterise exactly when it deadlocks. We
then show that send and fast_send each map to a distinct, natural mechanism on every
substrate we build:

- **CPU:** mailbox versus an inline call;
- **FPGA:** a FIFO versus a fused pipeline;
- **GPU:** a device ring versus a device function;
- **between substrates:** a bridge versus a blocking request/reply.

## What already exists (verified by search, 2026-09-30)

| work | what it has | how fast_send differs |
|---|---|---|
| Hewitt et al. 1973; Agha 1986; Agha, Mason, Smith, Talcott, *A foundation for actor computation*, JFP 1997 | the formal actor model, asynchronous send only, operational semantics, fairness | no synchronous primitive at all |
| ABCL/1 (Yonezawa et al., 1986) | three message types: *past* (asynchronous), *now* (send and wait for the reply), *future* | *now* blocks the sender while the receiver runs on its own thread; fast_send runs the receiver's turn on the sender's thread |
| Extended Rebeca (Sirjani, de Boer, Movaghar, Shali) | a component-based actor language with synchronous message passing and formal semantics | to read in full: how its synchronous send is scheduled, and what it proves |
| ABS / Creol (Johnsen, Hähnle et al., FMCO 2010) | asynchronous calls with futures between object groups (cogs); synchronous calls only inside a cog, where control passes to the callee; formal semantics of Core ABS | the closest match to fast_send inside an actor Group, but restricted to one cog and one processor; fast_send also works across threads under mutual exclusion, and across substrates |
| E language (Miller) | immediate call to objects in the same vat; eventual send, returning a promise, across vats | same split as ABS, restricted to one event loop, so no deadlock; no synchronous call across vats |
| De Boer et al., *A Survey of Active Object Languages*, ACM CSUR 2017 | the active-object family: futures, cooperative scheduling, synchronous calls | the map of this design space; our related-work section starts here |
| De Koster and De Meuter, *A Formal Specification for Half a Century of Actor Systems* (VUB TR 2024; Springer 2025) | one operational semantics for four actor families (classic actors, active objects, processes, communicating event loops); proves the *isolated turn principle* for all four | **the baseline to extend**: add a fast_send rule and prove isolated turns still hold |
| Honda and Tokoro 1991; Boudol 1992 | synchronous communication can be encoded in the asynchronous π-calculus | fast_send adds no expressive power in that sense, so the claim must be about who executes the turn, what it costs and what it guarantees, not about what can be computed |
| CAL / StreamBlocks (Bezati et al.) | dataflow actors compiled to both CPU and FPGA (via HLS) | multi-substrate actors, but dataflow actors with firing rules; no synchronous call and no send / fast_send distinction |
| Erlang `gen_server:call`, Akka `ask`, Scala actors `!?` | synchronous-looking request/reply built from two asynchronous messages, usually with a timeout | library patterns, not primitives; the receiver's turn still runs on its own thread |
| Mayeski, *Adapting the Actor Model of Concurrency for HFT: Synchronous Message Delivery (fast_send)*, arXiv 2609.21173 | fast_send, receiver transparency, groups, measured cost | our own prior paper: informal; this one formalises it and extends it to other substrates |

**The gap.**
- **Synchronous calls exist, but only in two forms.** Either the sender waits while the
  receiver runs on its own thread (ABCL/1 *now*, `gen_server:call`, `ask`), or a
  synchronous call is allowed only inside one scheduling unit (ABS cogs, E vats).
- **No formal treatment of our kind of primitive.** None that we found gives a primitive
  in which the sender's execution resource runs the receiver's turn, across threads,
  with mutual exclusion, formally connected to a unifying actor semantics.
- **No mapping to multiple substrates.** None maps such a primitive to hardware.

The search was not exhaustive. Extended Rebeca and the De Koster–De Meuter paper must be
read in full before claiming novelty.

## Formal contributions (to develop)

1. **Semantics.** Extend the De Koster–De Meuter operational semantics with a `fast_send`
   rule. The sender's turn is suspended, the receiver's turn runs to completion on the
   sender's execution resource, and the reply is returned. Possible models are nested
   turns, or a turn stack per execution resource.
2. **Isolation.** Prove that the isolated turn principle still holds: no two turns of
   the same actor overlap, and each turn sees only its own actor's state.
3. **Deadlock.** fast_send deadlocks if and only if the wait-for graph of suspended turns
   has a cycle. With asynchronous send only, the model is free of this kind of deadlock.
4. **Order.** A fast_send does not wait behind messages already in the receiver's mailbox.
   State exactly which order guarantees hold for mixed send / fast_send traffic, per
   sender–receiver pair.
5. **Equivalence.** fast_send is observationally equivalent to *send, then block until the
   reply* whenever no cycle exists. They differ in execution resource and cost, which is
   the point of the substrate mapping.
6. **Location.** fast_send is well defined whenever the caller can wait. Define "can
   wait" per substrate pair, and show which pairs admit it:
   - same substrate: an inline call;
   - towards a device or another machine: an RPC;
   - FPGA to CPU: a stalled process, which is allowed.

## Systems and evaluation (built or in progress)

- **CPU runtime** (`actors/cpp`): send, fast_send, groups; measured with
  `perf/bench_pingpong`.
- **Rust runtime and C++ ↔ Rust interop** (`actors/rust`): measured.
- **FPGA runtime** (`actors/fpga`): actor processes, mailbox FIFOs, a fast_send port,
  router, host link, and a CPU bridge with all four directions. Tested with a software
  card. Needs synthesis on Vitis for cycle counts.
- **GPU runtime:** design only (future work).
- **Evaluation table:** the cost of send and of fast_send for every pair of runtimes, with
  the FPGA numbers from synthesis and the PCIe numbers labelled as a model until a card
  is available.

## Paper outline

1. Introduction: one model, many substrates; why fast_send belongs in the model.
2. Background: actor semantics (Agha et al.; De Koster and De Meuter), the four families,
   isolated turns.
3. Why the actor model left synchronous send out: arrival order, deadlock, distribution.
   How existing systems work around it (ABCL/1, ABS, E, Erlang, Akka).
4. fast_send: definition and formal semantics.
5. Properties: isolation, deadlock characterisation, ordering, equivalence.
6. Mapping to substrates: CPU, FPGA, GPU, and between substrates, with the table.
7. Implementation: the CPU, Rust and FPGA runtimes, and the bridges.
8. Evaluation: the cost table.
9. Related work.
10. Conclusion.

## Open items

- Read in full: Extended Rebeca (synchronous messages); De Koster and De Meuter;
  Agha et al. 1997; the ABS synchronous-call rule; the De Boer et al. survey section on
  synchronous calls.
- Mechanisation: pen-and-paper proofs, or Coq / Isabelle / Maude (Rebeca and ABS both
  have Maude tooling, which may make the semantics reusable).
- Reconcile our own prior paper with the code. arXiv 2609.21173 describes a
  "lightweight cyclic-invocation detector" for fast_send, but the current
  `actors/cpp` code has only `assert(this != sender)` in `Actor::fast_send`, and no
  cycle detection. The formal deadlock result and the implementation must agree.
- Venue: a programming-languages or concurrency venue for the formal part (e.g.
  COORDINATION, FORTE, ECOOP, AGERE workshop); the systems numbers could go to a
  systems venue separately.
