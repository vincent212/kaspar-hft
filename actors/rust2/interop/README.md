# actors2 — C++/Rust Interop

Run C++ actors and Rust (`actors2`) actors **in one process**, talking over a C ABI,
**location-transparently**: an actor calls `send` / `fast_send` and never learns whether the
peer is C++ or Rust. This is feature-gated — build with `--features interop`; with the feature
off, `actors2` is pure in-process Rust and none of this is compiled.

```
Rust actor  ──send/fast_send──▶  ActorRef::Cpp  ──C ABI──▶  C++ actor
C++ actor   ──send/fast_send──▶  RustActorIF    ──C ABI──▶  Rust actor
```

## The one file you edit: `messages/interop_messages.h`

Every message that crosses the boundary is a POD C struct with an id annotation. This header is
the **single source of truth** — C++ `#include`s it directly, and the generator produces the
matching Rust from it, so the two languages can never disagree on layout.

```c
INTEROP_MESSAGE(DataRequest, 402)
typedef struct {
    int32_t        request_id;
    interop_string symbol;
} DataRequest;

INTEROP_MESSAGE(DataResponse, 403)
typedef struct {
    int32_t request_id;
    double  value;
    int32_t found;   /* bool: 1 = found, 0 = not found */
} DataResponse;
```

Rules (POD only — nothing that owns heap crosses FFI):

| Kind | Write | Becomes (Rust / C++) |
|------|-------|----------------------|
| Integer | `int32_t` / `int64_t` / `uint32_t` / `uint64_t` | `i32`/… / same |
| Float | `double` / `float` | `f64`/`f32` / same |
| **Bool** | `int32_t` **+ a `bool` comment on the same line** | `bool` / `bool` |
| **String** | `interop_string` (fixed 64-byte, no heap) | `String` / `std::string` |
| Array | `double prices[5];` | `[f64; 5]` / `std::array<double,5>` |

- Never use `int`/`long` (ABI-unstable width).
- Message **ids 400–499**, and must be `< 512` (also `< actors2` `HANDLER_CACHE_SIZE` = 1024).
  Ids `< 16` are reserved by `actors2` for framework messages.

## Add a message in three steps

1. Add an `INTEROP_MESSAGE(...)` + `typedef struct` to `messages/interop_messages.h`.
2. Regenerate:
   ```
   python3 interop/codegen/generate.py
   ```
3. Use the new type — it exists on both sides, with marshalling and dispatch already wired.

That's it. You never hand-write a `#[repr(C)]` mirror, a `to_c`/`from_c`, or a dispatch `match`
arm — and because both sides come from one file, a field you add or reorder can't silently
corrupt the other language.

## What the generator emits

`generate.py` reads the header and writes (all marked `AUTO-GENERATED — DO NOT EDIT`):

| File | Side | Contents |
|------|------|----------|
| `src/interop/generated.rs` | Rust | per message: `#[repr(C)]` wire struct + native struct + `define_message!` + `to_c`/`from_c`; the inbound dispatcher; the outbound marshalling; the C++ lookup; `register()` |
| `interop/generated/cpp/InteropMessages.hpp` | C++ | `msg::Name : Message_N<id>` with `to_c`/`from_c` |
| `interop/generated/cpp/CppActorBridge.hpp/.cpp` | C++ | the `extern "C"` `cpp_actor_*` the Rust side calls |
| `interop/generated/cpp/RustActorIF.hpp` | C++ | `interop::RustActorIF` — how C++ calls a Rust actor |

The Rust output is compiled and unit-tested here (`cargo test --features interop`, see
`tests/interop_codegen.rs`). The C++ output targets the public `actors/cpp` API
(`actors::Message_N<N>`, `Actor::send`/`fast_send`) and is compiled with a hybrid binary, not by
`cargo`.

## The contract the generated code plugs into

The generator never names `actors2` internals — it only fills in a small, fixed set of hooks
(hand-written, in `src/interop/mod.rs`). This is what keeps generated code from drifting against
the framework as messages are added:

**Rust side**
```rust
use actors2::interop::{generated, register_local_lookup};

let mut mgr = actors2::Manager::new();
mgr.manage("calc", Box::new(Worker), Default::default());

// 1. tell the inbound dispatcher how to resolve a local actor name -> ActorRef
let handle = mgr.get_handle();                       // Clone + Send + Sync
register_local_lookup(move |name| handle.get_ref_local(name));

// 2. install the generated inbound dispatch + C++ resolver
generated::register();

mgr.init();

// 3. reach a C++ actor location-transparently
if let Some(cpp) = mgr.get_ref("cpp_pricer", "calc") {
    cpp.send(Box::new(generated::DataRequest { request_id: 1, symbol: "BTC".into() }), None);
}
```

**C++ side** (built with the hybrid example)
```cpp
cpp_actor_init(&manager);                 // let Rust reach C++ actors by name
interop::RustActorIF rust_calc("calc", "cpp_pricer");
rust_calc.send(msg::DataRequest{ /* ... */ });
```

### `fast_send` across the boundary

`fast_send` to a C++ actor runs the C++ handler inline over FFI and returns **`None`** — a reply
cannot cross the C ABI synchronously, and `Option<MsgBox>` already means *"delivered, no reply."*
So it does **not** throw. If you need a value back, use `send` + a reply message (Phase 3 routes
replies across the boundary). Location-transparent request/reply should therefore use `send`, not
`fast_send`-for-a-return-value, since the same call may resolve to a C++ actor.

## Building a hybrid binary

The Rust and C++ objects are linked into **one** executable; the `cpp_actor_*` / `rust_actor_*`
symbols resolve at that final link. Build `actors2` as a static lib with `--features interop`,
compile the generated + hand-written C++ against `actors/cpp`, and link them together. See
[`examples/interop/`](../examples/interop/) for the layout and link model (the runnable
`Makefile` + C++ `main` land with the C++ hybrid phase — they are not in this change). The C++
side needs the `actors/cpp` build (boost); `cargo` alone does not produce the hybrid binary.

**Linking note:** with `--features interop` the crate references the C++ `cpp_actor_*` symbols,
left undefined in the Rust object and resolved at the final link against the C++ bridge. A
Rust-only build that enables the feature but links neither the C++ bridge nor a stub for those
symbols may fail on a strict linker (`--no-undefined` / `-z defs`); the Rust interop tests link
tiny `extern "C"` stubs for exactly this reason.

## Limits (Phase 2)

- **POD only** — no `String`/`Vec` across FFI beyond fixed `interop_string`.
- **No synchronous reply across FFI** — replies are async messages (cross-boundary reply routing
  is Phase 3).
- The C++ `RustActorRef::fast_send` in `actors/cpp` still `throw`s for non-local targets; the fix
  (deliver + return an empty `unique_ptr`) lands with the C++ side in a later phase.
