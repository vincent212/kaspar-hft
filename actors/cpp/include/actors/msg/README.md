<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Built-in Messages

This directory contains predefined message types used by the actor system.

## Start

**Header:** `Start.hpp`

Sent to actors when they are initialized by the Manager.

```cpp
MESSAGE_HANDLER(actors::msg::Start, on_start);

void on_start(const actors::msg::Start*) {
  // Actor initialization logic
}
```

---

## Shutdown

**Header:** `Shutdown.hpp`

Sent to actors for graceful shutdown. The message ID is fixed at 5.

```cpp
MESSAGE_HANDLER(actors::msg::Shutdown, on_shutdown);

void on_shutdown(const actors::msg::Shutdown*) {
  // Cleanup before termination
}
```

---

## Timeout

**Header:** `Timeout.hpp`

Sent when a timer expires. Used with `Timer::wake_up_in()` and `Timer::wake_up_at()`.

```cpp
MESSAGE_HANDLER(actors::msg::Timeout, on_timeout);

void on_timeout(const actors::msg::Timeout* t) {
  int data = t->data;  // Custom data from timer
}
```

---

## Continue

**Header:** `Continue.hpp`

A message an actor sends to itself to continue processing. Useful for breaking up long-running work or yielding to process other messages.

```cpp
MESSAGE_HANDLER(actors::msg::Continue, on_continue);

void on_start(const actors::msg::Start*) {
  // Start processing, send continue to self
  this->send(new actors::msg::Continue(0), this);
}

void on_continue(const actors::msg::Continue* c) {
  // Process next batch
  if (more_work()) {
    this->send(new actors::msg::Continue(c->id + 1), this);
  }
}
```

---

## Set

**Header:** `Set.hpp`

Set a named variable on an actor. Uses `std::any` for type-erased values.

```cpp
// Sender
other_actor->send(new actors::msg::Set("threshold", 0.5));

// Receiver
MESSAGE_HANDLER(actors::msg::Set, on_set);

void on_set(const actors::msg::Set* s) {
  if (s->varname == "threshold") {
    threshold = std::any_cast<double>(s->value);
  }
}
```

---

## Subscribe

**Header:** `Subscribe.hpp`

Subscribe to events from another actor. The receiving actor should track subscribers and notify them of events.

```cpp
// Subscribe to publisher
publisher->send(new actors::msg::Subscribe(), this);
```

---

## Creating Custom Messages

Inherit `MessageT<Derived>`. The dispatch id is auto-assigned and collision-free
by construction — **do not hand-pick an id.**

```cpp
#include "actors/Message.hpp"

struct MyMessage : public actors::MessageT<MyMessage> {
  int value;
  std::string text;

  MyMessage(int v, const std::string& t) : value(v), text(t) {}
};
```

Every type gets an integer id used for O(1) handler dispatch (`Actor::call_handler`
indexes `handler_cache` by it); with `MessageT` it's assigned on first use from a
counter starting at 512, and `get_message_id()` is a non-virtual member read.

> **Legacy:** `Message_N<N>` hard-codes the id to a compile-time constant. It is
> not uniqueness-checked, so two types can silently collide — use it only if you
> genuinely need the id as a constant expression (e.g. a `case` label), and run
> `setclassid/setclassid.py` to audit existing ones. New messages use `MessageT`.
