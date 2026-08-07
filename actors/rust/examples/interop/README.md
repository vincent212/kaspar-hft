# Interop example — C++ ↔ Rust in one process

A worked example of the cross-language interop: a **C++ actor and a Rust (`actors`) actor
exchanging messages in a single process** over the C ABI, location-transparently. Start with the
concepts in [`../../interop/README.md`](../../interop/README.md); this page is the build recipe.

## What runs today

The **Rust half is fully exercised now** by
[`tests/interop_codegen.rs`](../../tests/interop_codegen.rs), which drives the generated messages
in both directions against a pure-Rust mock of the C++ bridge (no C++ build needed):

```
cargo test --features interop
```

It sends a `DataRequest` inbound (simulating C++ → Rust) and marshals a `Ping` outbound
(Rust → C++), asserting both the delivered payload and the on-wire bytes. Read it as the
executable specification of the Rust side.

The **full hybrid binary** (a real C++ `main` linked with the Rust actors) is assembled in the
phase that builds the C++ side — its layout and link model are below.

## The flow

```
                 interop/messages/interop_messages.h        (one source of truth)
                                 │  python3 interop/codegen/generate.py
              ┌──────────────────┴───────────────────┐
   src/interop/generated.rs                interop/generated/cpp/*
   (Rust structs + dispatch)               (C++ classes + bridge)
              │                                       │
        actors (staticlib,                    actors/cpp objects
         --features interop)                   (+ generated bridge)
              └──────────────────┬───────────────────┘
                          one linked executable
             (cpp_actor_* / rust_actor_* resolve at final link)
```

## Hybrid layout (planned — added with the C++ hybrid phase)

These files are **not in this change yet** (the C++ side needs the boost `actors/cpp` build); the
runnable Rust half today is the test above. The intended layout:

```
examples/interop/ping_pong/
  interop_messages.h -> ../../../interop/messages/interop_messages.h   (shared header)
  rust_side.rs        the Rust actor + `register_local_lookup` + `generated::register()`
  main.cpp            the C++ actor + `cpp_actor_init(&mgr)` + `RustActorIF`
  Makefile            builds the Rust staticlib, compiles C++, links one binary
```

## Build model

1. **Rust → static library**, with interop on:
   ```
   cargo build --release --features interop     # produces libactors.a (+ your rust_side)
   ```
   The Rust side calls `cpp_actor_send` / `cpp_actor_fast_send` / `cpp_actor_exists`; those stay
   **undefined** in the Rust object and are resolved at the final link (below).

2. **C++ → objects**, compiling the generated bridge + your `main.cpp` against `actors/cpp`
   (needs the boost-based C++ build). The C++ side calls the exported
   `rust_actor_send` / `rust_actor_fast_send` / `rust_actor_exists`, which `actors` provides.

3. **Link them into one executable.** Because both halves are in the same binary, every
   `cpp_actor_*` and `rust_actor_*` symbol resolves, and messages flow both ways.

At startup each side registers the other:

```rust
// Rust
register_local_lookup(move |name| handle.get_ref_local(name));
actors::interop::generated::register();
```
```cpp
// C++
cpp_actor_init(&manager);
```

## Notes

- **Location transparency:** application code calls `send` / `fast_send` on an `ActorRef` and
  never branches on "is this C++ or Rust." `Manager::get_ref(name, sender)` returns a local actor
  if there is one, else a C++ actor over FFI.
- **`fast_send`** to a cross-language actor delivers inline and returns `None` (no synchronous
  reply across the C ABI) — use `send` + a reply message for a value back.
- Adding a message is always: edit the header → `python3 interop/codegen/generate.py` → rebuild.
