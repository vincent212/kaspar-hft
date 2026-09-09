# Killing the Mailbox Virtual Call: A Design

## The cost, measured honestly

Today the actor stores its mailbox as a base pointer and every op is virtual:

```cpp
Queue<const Message*>* msgq;   // Actor.hpp:160
msgq->push(m);                 // Actor.cpp:203  (producer)
auto r = msgq->pop();          // Actor.cpp:158  (consumer run loop)
```

Two things are true, and the second matters more than the first:

1. **On `push`, the virtual call is noise.** `push` already takes a mutex
   (BQueue/ShardedBQueue) or does a CAS (LockFreeMPSC) — tens of nanoseconds. A
   1–2 ns indirect call next to that is in the mud. And `push` is reached only
   through `Actor*` (`send()` → `add_message_to_queue()` → `msgq->push()`), so
   the producer can't see the concrete queue type anyway. Don't optimize here.

2. **On the consumer, the indirection blocks *inlining*, and that's the prize.**
   The run loop pops and dispatches in a tight loop. `Queue<T>*` forces an
   indirect call per pop and — worse — stops the compiler from fusing
   `pop_batch` with the per-message handler dispatch. Inlining the drain loop is
   worth far more than the single call it removes.

So the goal is not "delete a virtual call." It's **make the consumer drain loop
monomorphic so the compiler can inline it**, and stop paying vtable indirection
on the hot path — while keeping `Actor` reachable as `Actor*` for the scheduler,
`Manager`, `Group`, and every `send()`.

## The hard constraint

`send(const Message*, Actor* sender)` and the scheduler all traffic in `Actor*`.
You cannot template `Actor` itself away — the moment `Actor<BQueue>` and
`Actor<LockFreeMPSC>` are different types, `Group`'s actor list, `Manager`, and
`target->send(...)` stop compiling. Whatever we do must keep a **non-template
`Actor` handle** for everyone who isn't the actor's own consumer thread.

That splits the problem: the *consumer* (the actor's own `run()`) can know its
concrete queue; the *producer* (anyone holding `Actor*`) cannot. Design for each
separately.

---

## Why not CRTP

CRTP (`class BQueue : Queue<BQueue>`) gives static dispatch but requires the base
to be templated on the derived type — which is exactly what breaks heterogeneous
`Actor*` storage. It's fine *inside* a queue family, useless at the Actor↔Queue
boundary where we need a common handle. Rejected for this seam.

---

## Design A — `std::variant` mailbox, visit once, then drain (recommended)

The queue set is **closed and tiny** (BQueue, ShardedBQueue, LockFreeMPSC).
Virtual dispatch exists for *open* hierarchies; for a closed set the idiomatic
zero-indirection tool is a tagged union: `std::variant` + `std::visit`. `Actor`
stays a single concrete type.

```cpp
using Mailbox = std::variant<BQueue<const Message*>,
                             ShardedBQueue<const Message*>,
                             LockFreeMPSC<const Message*>>;

class Actor {
  Mailbox msgq;                 // by value; one Actor type, no template explosion
public:
  // Producer path: one visit. Compiles to a switch on the discriminant, and
  // each arm's push() is INLINED into the arm — no vtable, no indirect call.
  void add_message_to_queue(const Message* m) noexcept {
    std::visit([m](auto& q){ q.push(m); }, msgq);
  }

  // Consumer path: visit ONCE, then run the entire loop against the concrete
  // queue. The whole drain loop is monomorphic and inlinable.
  void run() {
    std::visit([this](auto& q){ drain_loop(q); }, msgq);
  }
  template <class Q>
  void drain_loop(Q& q) {
    std::vector<const Message*> batch;
    while (true) {
      q.pop_batch(batch);       // direct call, fused with the dispatch below
      std::lock_guard lk(fast_send_mutex);
      for (auto* m : batch) { /* call_handler / process_message */ }
      if (terminated) break;
    }
  }
};
```

**How you pick a queue** stays a runtime decision — construct the variant with
whichever alternative the config asks for:

```cpp
if (cfg.queue == "lockfree") msgq.emplace<LockFreeMPSC<const Message*>>(ring_sz);
else                         msgq.emplace<BQueue<const Message*>>();
```

**Pros**
- **Consumer loop is monomorphic and inlinable** — visit once, then the compiler
  sees a concrete `Q&` for the whole loop and fuses `pop_batch` with dispatch.
- **No indirect call on `push`** — `std::visit` lowers to a jump table; each arm
  is inlined. A predicted branch beats an indirect call for the pipeline.
- **`Actor` stays one type.** `Group`, `Manager`, `send()`, `Actor*` — all
  unchanged. No template explosion, no code bloat per actor type.
- **Runtime selection preserved** — queue chosen per *instance* from config,
  exactly like today.

**Cons**
- Every `Actor` is sized for the **largest** alternative (union storage). With
  three queue types that's a few hundred bytes of slack per actor — cheap.
- The queue set becomes **closed**: adding a fourth type means editing the
  `variant` (a compile-time change, not a plugin). For an in-house framework
  with three known queues, that's a feature, not a limit.
- `std::visit`'s codegen quality varies by compiler/version — **measure** it
  against the vtable baseline; on a tiny `push` it can be a wash (the point is
  the inlined consumer loop, not the push).

---

## Design B — non-template base + templated `ActorQ<Q>` (if you'll fix the queue at compile time)

Split the class in two: a polymorphic base for everyone else, a templated
derived that owns the queue by value.

```cpp
class Actor {                                  // scheduler/sender-facing handle
public:
  virtual void enqueue(const Message* m) noexcept = 0;  // ONE virtual at the Actor* boundary
  virtual void run() = 0;
  // handlers, handler_cache, lifecycle live here (not templated)
};

template <class Q>
class ActorQ : public Actor {
  Q msgq;                                       // concrete, by value
public:
  void enqueue(const Message* m) noexcept override { msgq.push(m); }  // push() inlined inside
  void run() override {                          // fully devirtualized drain loop
    std::vector<const Message*> batch;
    while (true) { msgq.pop_batch(batch); /* dispatch */ }
  }
};

// A user actor bakes its queue into its type:
class OB : public ActorQ<LockFreeMPSC<const Message*>> { /* handlers */ };
```

**Pros**
- Consumer loop fully devirtualized/inlined (same win as A).
- Producer path drops from **two** virtual calls to **one**: `target->enqueue()`
  is the only indirection; the inner `msgq.push()` is inlined inside it. (Today
  it's virtual `send` *and* virtual `push`.)

**Cons**
- **Queue is chosen at compile time, per actor *type*** — you lose runtime
  per-instance selection. `OB` *is* a lockfree-mailbox actor forever.
- Some template bloat: `ActorQ<BQueue>` and `ActorQ<LockFreeMPSC>` are distinct
  instantiations of the run loop.
- User actors must slot into the `Actor → ActorQ<Q> → UserActor` hierarchy —
  a real refactor of every existing actor.

---

## Recommendation

Go with **Design A (`std::variant`)**. It removes the vtable indirection, makes
the hot consumer loop inlinable, keeps runtime per-instance queue selection, and
— crucially — leaves `Actor` a single type so `Group`/`Manager`/`send()` don't
change. The closed queue set is a fact of this codebase, which is exactly when a
tagged union beats an inheritance hierarchy.

Reach for **Design B** only if you're willing to fix each actor's queue at build
time and want the producer `enqueue` collapsed to a single indirection too.

**But lead with a measurement.** The per-`push` virtual call is dwarfed by the
lock/CAS it sits next to; the win that justifies this work is inlining the
consumer drain loop (and, for `fast_send`, note the queue isn't touched at all —
none of this changes the inline fast path). Benchmark the variant consumer loop
against the `Queue<T>*` baseline on `bench_pingpong` / `bench_dispatch` before
committing to the refactor. If the numbers don't move, the cleanest thing is to
leave the vtable in place and spend the complexity budget elsewhere.
