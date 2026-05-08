<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Actor Utilities

This directory contains utility classes for building actor systems.

## Manager

**Header:** `Manager.hpp`

The Manager coordinates actor lifecycle - registration, startup, and shutdown.

### Usage

```cpp
#include "actors/act/Manager.hpp"

class MyManager : public actors::Manager {
public:
  MyManager() {
    // Register actors in constructor
    manage(new WorkerActor());
    manage(new LoggerActor(), {0}, 50, SCHED_FIFO);  // CPU 0, priority 50
  }
};

int main() {
  MyManager mgr;
  mgr.init();  // Start all actors
  mgr.end();   // Wait for completion
}
```

### Key Methods

| Method | Description |
|--------|-------------|
| `manage(actor, affinity, priority, sched_type)` | Register an actor |
| `init()` | Start all actors |
| `end()` | Wait for all actors to finish |
| `get_actor_by_name(name)` | Find actor by name |
| `total_queue_length()` | Get pending message count |

---

## Group

**Header:** `Group.hpp`

Run multiple lightweight actors in a single thread. Use when actors don't need dedicated threads.

### Usage

```cpp
#include "actors/act/Group.hpp"

actors::Group grp("my_group");
grp.add(new LightActor1());
grp.add(new LightActor2());
grp.add(new LightActor3());

mgr.manage(&grp);  // All 3 actors share one thread
```

### When to Use

- Actors that process messages quickly
- Reducing thread count for many small actors
- Actors that need sequential message processing

---

## Timer

**Header:** `Timer.hpp`

Schedule delayed messages to actors.

### Usage

```cpp
#include "actors/act/Timer.hpp"

// Wake up in 5 seconds
actors::Timer::wake_up_in(my_actor, 5);

// Wake up in 2.5 seconds
actors::Timer::wake_up_in(my_actor, 2, 500);

// Wake up at next 1-second boundary (aligned timing)
actors::Timer::wake_up_at(my_actor, 1000);

// Pass custom data with timeout
actors::Timer::wake_up_in(my_actor, 1, 0, 42);  // data=42
```

### Handling Timeouts

```cpp
class MyActor : public actors::Actor {
public:
  MyActor() {
    MESSAGE_HANDLER(actors::msg::Timeout, on_timeout);
  }

  void on_timeout(const actors::msg::Timeout* t) {
    int data = t->data;  // Custom data passed to wake_up_*
    // Handle timeout...
  }
};
```

### Methods

| Method | Description |
|--------|-------------|
| `wake_up_in(actor, secs, msecs, data)` | Send Timeout after delay |
| `wake_up_at(actor, interval_ms, data)` | Send Timeout at next interval boundary |
| `sleep(secs, msecs)` | Block current thread |
