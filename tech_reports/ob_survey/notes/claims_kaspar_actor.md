# Claims: building an order-book model actor in Kaspar

How to add an actor that receives book updates, computes a prediction, and passes it to a strategy (light22), using only APIs that exist in this tree. Every factual line cites file:line, relative to `/Users/vm/kh-obsurvey`. Anything not in the tree is labelled **NEW**. Anything I worked out by reading code but did not run is labelled **DERIVED (untested)**.

---

## 1. Actor basics (framework: `actors/cpp`)

- **Base class.** `actors::Actor` (actors/cpp/include/actors/Actor.hpp:103). It has a protected `char name[256]` (Actor.hpp:168), and `get_name()` is virtual (Actor.hpp:144). The default name is the mangled typeid (actors/cpp/Actor.cpp:45-48), so a real actor sets its own name.
- **Handler registration.** Call `MESSAGE_HANDLER(Type, method)` in the constructor (macro at Actor.hpp:59-63). It stores the handler in `handlers[type_index]` (Actor.hpp:268-272). Dispatch goes through `handler_cache[id]`, which falls back to RTTI on a cache miss (Actor.cpp:76-97). A message type with no registered handler falls through to the virtual `process_message()` (Actor.cpp:108-110; Actor.hpp:173).
- **`send(m, sender)`** is asynchronous (Actor.hpp:127; Actor.cpp:51-74):
  - It asserts `m->destination == nullptr`, i.e. "cannot reuse message" (Actor.cpp:62). Each recipient therefore needs its own `new` message.
  - If the target is in a Group, the message goes to the **group's** queue (Actor.cpp:69-73).
  - Messages sent to a terminated actor are dropped silently (Actor.cpp:58-59).
  - After the handler runs, the framework deletes the message unless the handler set `no_delete` (Actor.cpp:114-116).
- **`fast_send(m, sender)`** is synchronous (Actor.cpp:119-143):
  - It takes the **target's** `fast_send_mutex` and runs the target's handler on the **caller's** thread.
  - It returns whatever the handler passed to `reply()`, as a `unique_ptr`.
  - It does **not** delete `m`. Existing code passes a stack message, e.g. `frame::som::msg::Cancel cancel_msg(...); som->fast_send(&cancel_msg, this);` (light/include/light/act/light22_base.hpp:359-360).
  - It asserts if an actor fast_sends to itself (Actor.cpp:126).
  - The async dispatch path takes the same mutex (Actor.cpp:101), so a fast_send and the target's own thread are serialized against each other.
- **`reply(m)`** (Actor.cpp:227-236): under a fast_send it stores the reply for the caller. Otherwise it does `reply_to->send(m, this)`, where `reply_to` is set from `m->sender` in the run loop (Actor.cpp:192).
- **Manager lifecycle** (`actors::Manager : Actor`, actors/cpp/include/actors/act/Manager.hpp:58):
  - `add_to_manage_q(actor, affinity, prio)` (Manager.hpp:104-107) only queues the actor. It asserts that names are unique (actors/cpp/Manager.cpp:217-230).
  - `init()` first fast_sends `actors::msg::Start` to each actor (Manager.cpp:132-137), then starts one `std::thread` per managed actor (Manager.cpp:139-142).
  - The thread body calls the virtual `init()` and then the drain loop (Actor.cpp:216-221). For top-level actors, Start is therefore handled **before** `init()` runs.
  - `end()` sends `Shutdown` to every actor and joins the threads (Manager.cpp:177-195).
  - `manage_and_start()` exists for actors added after `init()` (Manager.hpp:116-119; Manager.cpp:425).
- **Group** (`actors::Group : Actor`, actors/cpp/include/actors/act/Group.hpp:75): several actors share one thread.
  - `add(a)` sets `a->set_group(this)` and records the actor by name (actors/cpp/Group.cpp:43-50).
  - The Group itself is managed. `add_to_manage_q(group)` asserts that the group already has members (Manager.cpp:261-271). So add every member first, then manage the group.
  - On Start, the group calls `a->init()` and then `a->fast_send(new Start, this)` for each member, in add order (Group.cpp:125-134).
  - On Shutdown, it fast_sends Shutdown to each member and calls `end()` (Group.cpp:142-156).
  - `add_and_start(a)` is for actors added after the group has started (Group.cpp:65-75). Plain `add` does not send Start.
  - Delivery: a message for a member goes into the group mailbox. The group thread pops it and calls `Group::process_message`, then `forward`, then `m->destination->process_message_internal(m, true)` (Group.cpp:171-238).
  - The Group registers its own handlers for `Start`, `Shutdown`, `ShutdownThisActor` and `AddActor` (Group.cpp:53-56). Messages of those types sent to members are intercepted by the Group, so a new actor should not send those types to its peers.
- **Wall-clock timer warning.** `actors::act::Timer::wake_up_in` spawns a thread that sleeps in wall-clock time (actors/cpp/include/actors/act/Timer.hpp:29-32). That is **not** the market-time timer described in §4.

## 2. Message types

- `MessageT<Derived>` assigns the id automatically from a counter that starts at 512 (actors/cpp/include/actors/Message.hpp:130, 153-172, 198-204). Ids are dense, not stable across runs, and capped at 2048 (Message.hpp:136, 176-183).
- `Message_N<N>` uses a fixed id, which a `static_assert` restricts to [0, 512) (Message.hpp:142-149). Cross-type uniqueness is **not** checked (Message.hpp:121-124).
- Real examples, all `MessageT`:
  - `frame::ob::msg::EndOfBurst` (frame_kaspr/include/frame/ob/msg/EndOfBurst.hpp:16-28). It carries `boost::intrusive_ptr<const data_pay_load> payload` and uses `actors::MemoryPool` for allocation.
  - `light::msg::Set` (light/include/light/msg/Set.hpp:16-33). Its fields are an `action_t key` and a `double dval`.
  - `frame::mda::msg::Subscribe` (frame_kaspr/include/frame/mda/msg/Subscribe.hpp:20-39).

## 3. How consumers receive book updates today

### 3a. There is no generic pub/sub. Each book keeps its own subscriber lists.

- A subscriber sends `frame::mda::msg::Subscribe(prio)` to the book, with itself as sender.
- The `Subscribe` constructor takes `bool _prio` (Subscribe.hpp:34). Any non-zero enum value therefore narrows to 1 (`HI`):
  - `AGGR`(2) and the default `DATA`(3) both become HI.
  - Only `LOW`(0) is distinct.
  - TachBook.hpp:1256-1258 documents the same point.
- `BBBOSub` is a separate subscription for best-bid/best-offer changes (frame_kaspr/include/frame/ob/msg/BBBOSub.hpp:18).

### 3b. OB (MBP book, `frame/ob/act/OB.hpp`, `frame_kaspr/src/OB.cpp`)

- OB does not register `Subscribe` or `BBBOSub` with `MESSAGE_HANDLER`. They are handled in `OB::process_message`:
  - `BBBOSub` appends the sender to `bbbosubs` (OB.cpp:1429-1433).
  - `Subscribe` with HI goes to `hiprio_datasubs`, and with LOW to `lowprio_datasubs` (OB.cpp:1434-1447).
  - A subscriber that appears in both lists triggers an assert (OB.cpp:1451-1468).
  - **No check on the subscriber's name.**
- Each list gets different messages:
  - **HI subscribers** get `EndOfBurst(last_good_payload)` once per burst, via `publish_delayed` (OB.cpp:3078-3082). This is skipped when `point_.baddata` is set (OB.cpp:3065, 3085-3088). HI subscribers also get `TradeNotify` (OB.cpp:2840-2845) and `GapDetected` by **fast_send** (OB.cpp:3213-3216).
  - **LOW subscribers** get `EndOfBurst2`, which is top-of-book only, plus txtim (OB.cpp:3097-3130; struct at EndOfBurst2.hpp:21-63). They get `GapDetected` by `send` (OB.cpp:3217-3220).
  - **bbbosubs** get `BBBOChg`, throttled to at most once every 100 ms of market time and suppressed when the book is locked or crossed (OB.cpp:1366-1419).
- `publish_delayed` sends immediately when `feed_delay <= 0`. Otherwise it holds the message in `pub_q` until a later record's market time passes `now + feed_delay` (OB.hpp:284-307).
- Payload contents:
  - OB writes 16-level (`NLEVELS`) arrays into the payload's `point_`: `bid_px`, `ask_px`, `bid_sz`, `ask_sz` (OB.cpp:2763-2766).
  - The `point` struct is defined at frame_ref/include/frame/mda/msg/point.hpp:156-168, with `NLEVELS 16` at point.hpp:22.
  - The payload also carries `txtim_epoch`, `sym`, `px`, `sz`, `side`, `action`, `mev`, `ex_order_id`, `recovery` (frame_ref/include/frame/mda/msg/Data.hpp:56, 131-144).
  - The payload is shared by reference count across all subscribers (`boost::thread_safe_counter`, Data.hpp:46).
- Vestigial flag: OB has a `has_pred` flag (OB.hpp:67) that is always false (OB.cpp:162 is commented out, OB.cpp:173 sets false). It only switches which fields go into EndOfBurst2 (OB.cpp:3100-3130). **There is no prediction plumbing in OB.**

### 3c. TachBook (MBO book, `frame_kaspr/include/frame/ob/act/TachBook.hpp`)

- `Subscribe` and `BBBOSub` **are** registered with `MESSAGE_HANDLER` (TachBook.hpp:254-255).
- **Subscriptions are gated on the subscriber's name** (TachBook.hpp:369-428):
  - Every subscriber's name must contain "aggr", "MTD", "Timer" or "perf", and must not contain "super". Otherwise ASSERTF fires (TachBook.hpp:373-391).
  - HI is allowed only for names containing "aggr" or "perf" (TachBook.hpp:417).
  - LOW is allowed only for MTD or Timer (TachBook.hpp:409).
  - AGGR always raises ERRF (TachBook.hpp:393-395).
- `publish_book` behaviour:
  - Drops points with `baddata` (TachBook.hpp:1223-1227) and points equal to the previous one (TachBook.hpp:1230-1239).
  - Sends `new EndOfBurst(pl)` to each HI subscriber (TachBook.hpp:1269-1272).
  - Sends `EndOfBurst2` to LOW subscribers on every 4th publish only (TachBook.hpp:1285-1304).
  - Sends `BBBOChg` when the best bid or offer changes (TachBook.hpp:1313-1330).
- Existing passive subscriber that serves as a working example: `LatencyProbe`.
  - Its name is `perf_LatencyProbe_<tag>` (frame_kaspr/include/frame/perf/act/LatencyProbe.hpp:817).
  - It handles `EndOfBurst` and `TradeNotify` (LatencyProbe.hpp:822-823).
  - It subscribes HI in its Start handler (LatencyProbe.hpp:832-838).
  - The comment says the name must contain "perf" (interface/frame/perf/if/LatencyProbe.hpp:31-33).
- **In kaspr, TachBook receives no market data by default.** `handler_if` has one book vector and it is set to the OBs (kaspr/src/kaspr.cpp:484-486). It is switched to TachBooks only when `tachbook`, `perf_probe` and `perf_route_tachbook` are all set, and the code comments say this starves the OBs, lights, SOM, DB and MTD (kaspr.cpp:488-507).
- `SimKaspr` builds OBs only, no TachBook (sim/src/SimKaspr.cpp:245).

### 3d. How light22 subscribes

- light22 is created with `_ob` (interface/light/if/light22.hpp:24). Its Start handler sends `ob->send(new Subscribe(Subscribe::HI), this)` (light/include/light/act/light22.hpp:300-303).
- It registers `EndOfBurst` with `eob_handler` (light22.hpp:220-221). In both kaspr and sim, `_ob` is an **OB** (kaspr.cpp:415, 429; SimKaspr.cpp:435, 446).
- Light names look like `L_<sym>_BUY_<i>` (kaspr.cpp:410). Those names would fail TachBook's name gate (TachBook.hpp:389-391), so **light22 cannot subscribe to TachBook as currently named**. DERIVED (untested).
- A second working example of a non-light model-style subscriber in the sim group is `SlippageProbe`:
  - It handles EndOfBurst, TradeNotify, Alarm and Fill (sim/src/SlippageProbe.cpp:58-63).
  - It subscribes HI to the OB (SlippageProbe.cpp:97).
  - It arms a periodic market-time alarm (SlippageProbe.cpp:94).
  - It is added to the group at SimKaspr.cpp:321-322.

## 4. Other inputs light22 receives today. There is no external-signal hook.

- Handlers registered in `light22_base`'s constructor (light22_base.hpp:325-338): `som::msg::CancAck`, `Fill`, `Reject`, `CancReject`, `actors::msg::Shutdown`, `ob::msg::Clear`, `mtim::msg::Alarm`, `light::msg::Start`, `light::msg::Stop`, `light::msg::Set`, `ob::msg::GapDetected`, `ob::msg::TradeNotify`, `light::msg::GetLightInfo`, `light::msg::PositionInfo`.
- light22 itself adds `actors::msg::Start` and `EndOfBurst` (light22.hpp:220-221).
- **Timers.** `timer->send(new frame::mtim::msg::AlarmClockSub(s, ms, id, periodic), this)` (light22_base.hpp:526; light22.hpp:848-851). The resulting `Alarm` is dispatched by `timer_id` (light22_base.hpp:627-642; light22.hpp:230-280).
  - The Timer is `frame::mtim::act::Timer`. It subscribes **LOW** to one book (frame_kaspr/src/Timer.cpp:36-46).
  - It sets `currtim` from `EndOfBurst2::txtim`, which is market time (Timer.cpp:48-58).
  - It fires alarms stamped with that market time (Timer.cpp:94-124).
  - It is a process singleton (Timer.cpp:27).
- **QCoord / PCoord are shared memory, not messages.** They are plain structs with `std::recursive_mutex` (light/include/light/qcoord.hpp:24-31, 191-200), passed in as raw pointers (light22.hpp factory args, interface/light/if/light22.hpp:22-23), and read directly, e.g. `pcoord->get_position()` (light22_base.hpp:457; light22.hpp:335).
- **The only existing external numeric input is `light::msg::Set`.**
  - Its keys are fixed: TRADING_ON, TRADING_OFF, DUMP, LEV_ORDERS_MAX, TARGET_POS, SUBSCRIBEONLY (Set.hpp:18-26).
  - `TARGET_POS` sets `targetpos` (light22_base.hpp:480-484). That is a position target, not a model score.
  - In this tree, `Set::TARGET_POS` messages are constructed only in unit tests (grep: unit_test/src/test_light22.cpp:95, test_slippage_probe.cpp:268).
  - An unknown key hits `SNGH` (light22_base.hpp:515-520).
- **Where a prediction would be used.** The decision path is `eob_handler`, which calls `place_if_can_impl` on a same-side ADD and `canc_if_must_impl` on a CANC (light22.hpp:872-883).
  - Placement is gated by `place_rate_bp` against a per-light RNG (light22.hpp:561-580).
  - That RNG is seeded deterministically from `rng_seed` and the light's name (light22.hpp:206-216).
  - A prediction gate would be **NEW code** in `place_if_can_impl` or `eob_handler`.
- SHADOW_ALGORITHM.md:59 says of the light: "No model evaluation".

## 5. Threading, ordering, determinism

- **kaspr (live / paper).** Every actor is its own top-level managed actor with its own thread. kaspr.cpp contains no Group: books at kaspr.cpp:175 and 238, lights at 418 and 432, SOM at 361.
  - A predictor sending to a light would race with the OB's EndOfBurst going to that same light. The two come from different producer threads into one mailbox (BQueue is mutex + condvar, Actor.hpp:24, 199-204).
  - Relative order is therefore **not deterministic**. DERIVED.
- **SimKaspr.** Logger, OBs, Timer, probe, SOM, lights, PositionManager and BFA all sit in one `Group("sim_group")` (SimKaspr.cpp:79, 246, 287, 322, 336, 437/443, 534, 569), and the group is managed last (SimKaspr.cpp:101).
  - All sends between members go into the single group FIFO (Actor.cpp:69-70) and are processed one at a time on one thread (Group.cpp:220-238).
  - BFA reads **one record per `Continue`** it sends to itself (frame_kaspr/include/frame/mda/act/BFA.hpp:161-164, 166-238), and sends `Data` to the book with `send` (BFA.hpp:815-837).
  - So interleaving is a function of the input and of the add/subscribe order, not of OS scheduling. DERIVED.
  - The other sources of determinism are the market-time Timer (§4) and the deterministic light RNG (light22.hpp:206-216). The guide states that timing comes from data timestamps (STRATEGY_SIMULATOR_GUIDE.md:84-85).
- **Ordering of a `send`-based prediction in the sim group** (DERIVED (untested) trace from the cited code):
  1. OB handles `Data_t`. It enqueues `EndOfBurst_t` to every HI subscriber in one loop (OB.cpp:3078-3082). The loop runs in subscription order, which is the group add order, because members get Start in add order (Group.cpp:125-134) and each Start sends Subscribe.
  2. BFA's `Continue` then enqueues `Data_{t+1}`.
  3. The light handles `EOB_t`. The predictor handles `EOB_t` and enqueues `Pred_t` **behind** `Data_{t+1}`.
  4. OB processes `Data_{t+1}` and enqueues `EOB_{t+1}` **behind** `Pred_t`.
  - **Result:** a light acting on `EOB_t` has seen predictions up to `t-1` only. `Pred_t` arrives before `EOB_{t+1}`. With `feed_delay > 0`, EOBs are additionally held in `pub_q` (OB.hpp:284-307).
- **Ordering with `fast_send` instead.** If the predictor is added (and so subscribes) before the lights and calls `light->fast_send(&pred, this)`, the light's prediction handler runs nested inside the predictor's `EOB_t` handler. That is before the light's own `EOB_t`.
  - This is safe inside a Group: the predictor's dispatch holds only the predictor's mutex (Actor.cpp:101), and fast_send takes the light's (Actor.cpp:121).
  - In kaspr, the same fast_send would run light code on the predictor's thread, serialized with the light's thread by the light's `fast_send_mutex` (Actor.cpp:101, 121).
  - DERIVED (untested).
- **Things that break determinism:** wall-clock calls such as `chutil::Time::epoch()`, used by LatencyProbe (LatencyProbe.hpp:1109), and `actors::act::Timer` (§1). A predictor should take time from `payload->txtim_epoch` or from `Alarm::currtim`.

## 6. Where actors are created and wired

- **kaspr/src/kaspr.cpp**, constructor order (kaspr.cpp:122-138):
  1. Logger
  2. `create_order_books()`: OB per ES asset, `new frame::ob::act::OB(nullptr,false,this,j,pt)` (kaspr.cpp:157, 166-176)
  3. [USE_TACHBOOK] `create_tach_books()` (kaspr.cpp:230-239) and `create_probes()` (kaspr.cpp:284-286)
  4. `create_support_modules()`: CONS, MQ0, and `Timer(es_order_books[0])` (kaspr.cpp:314-320)
  5. `create_som()` (kaspr.cpp:351-362)
  6. `create_db()`
  7. `create_lights()`: one PCoord per symbol, one QCoord per side, 4 BUY + 4 SEL via `create_light22_Shadow_*`, each `add_to_manage_q` (kaspr.cpp:383-437)
  8. positionman, MTD
  9. `start_market_data()`, which builds `handler_if` and points it at the OBs (kaspr.cpp:484-486)

  `main` calls `mgr.init()` (kaspr.cpp:770) and `mgr.end()` (kaspr.cpp:780).
- **sim/src/SimKaspr.cpp**, constructor order (SimKaspr.cpp:76-101):
  1. Logger
  2. Group
  3. universe
  4. OBs `group_->add(ob)` (SimKaspr.cpp:245-246)
  5. Timer(books_.front()) (SimKaspr.cpp:286-287)
  6. probe
  7. SOM (SimKaspr.cpp:333-336)
  8. lights (SimKaspr.cpp:431-450, aggressive bank at 496-518)
  9. PositionManager
  10. BFA, added last: "nothing should receive data before it is ready" (SimKaspr.cpp:566-569)
  11. `add_to_manage_q(group_)` (SimKaspr.cpp:101)
- **sim/src/main.cpp** constructs `SimKaspr` (main.cpp:270-287) and runs it with `boost::thread(boost::ref(*mgr))` (main.cpp:297-298).

## 7. Factory-function convention

- The declaration goes in `interface/<lib>/if/<X>.hpp` and the definition in `<lib>/src/<X>.cpp`, returning `actor_ptr` (an alias for `actors::Actor*`, Actor.hpp:73).
  - Example: `create_light22_Shadow_BUY` is declared at interface/light/if/light22.hpp:13-32 and defined at light/src/light22.cpp:12-53.
  - Example: `create_LatencyProbe` is declared at interface/frame/perf/if/LatencyProbe.hpp:48-54 and defined at frame_kaspr/src/LatencyProbe_if.cpp:13-23.
- The build adds `-I$(INSTALL_PATH)/interface` (mk_kaspr/glob_begin.mk:96), so code includes these as `"light/if/light22.hpp"` (SimKaspr.cpp:24).
- Sources in a library are listed in `LIBSRC` (light/src/Makefile:8).
- Direct `new` without a factory is also used in the wiring files, e.g. OB (kaspr.cpp:166), Timer (SimKaspr.cpp:286) and SlippageProbe (SimKaspr.cpp:321).

---

## Recipe (steps 1 to 7)

1. **NEW message.** Add `light/include/light/msg/Prediction.hpp` deriving from `actors::MessageT<Prediction>`. Do not hand-pick an id (§2).
2. **NEW actor.** Add `light/include/light/act/ImbalancePredictor.hpp`, deriving from `actors::Actor` (§1).
   - Register `actors::msg::Start` and `frame::ob::msg::EndOfBurst`.
   - In Start, send `Subscribe(Subscribe::HI)` to the OB. This is the same call light22 makes (light22.hpp:303) and SlippageProbe makes (SlippageProbe.cpp:97).
   - If the source is a TachBook, the actor's name must contain "aggr" or "perf" (TachBook.hpp:388-391, 417), or TachBook's gate must be changed. Both are hacks or new code.
3. In `eob_handler`, read `m->payload->point_` (bid/ask px and sz, 16 levels) and skip `baddata` and `recovery`, as light22 does (light22.hpp:809-823). Compute the value and create one `new Prediction` **per light** (Actor.cpp:62 forbids reuse). Either `send` it (async, one EOB behind, see §5) or `fast_send` a stack instance (synchronous).
4. **NEW handler in light22.** In `light22_base`'s constructor (light22_base.hpp:324-338) add `MESSAGE_HANDLER(msg::Prediction, prediction_handler)`. Store the value in a member, and gate `place_if_can_impl` and/or `canc_if_must_impl` on it (light22.hpp:333, 872-883). Nothing like this exists today (§4).
5. **NEW factory** (optional but conventional). Declare `create_ImbalancePredictor(...)` in `interface/light/if/ImbalancePredictor.hpp`, implement it in `light/src/ImbalancePredictor.cpp`, and add that file to `LIBSRC` (light/src/Makefile:8).
6. **Wire in sim.** In `SimKaspr::create_lights` (or a new `create_predictor()`), create one predictor per book and call `group_->add(pred)`. This must happen **before** `create_bfa()` (SimKaspr.cpp:99, 569). Add it before the lights if you use `fast_send` and want the prediction ahead of the light's own `EOB_t` (§5).
7. **Wire in kaspr.** Inside `create_lights_for_ob` (kaspr.cpp:383-437), `new` the predictor with the light pointers and call `add_to_manage_q(pred)`. It gets its own thread. Ordering against EOBs is not deterministic (§5).

## Minimal sketch

Every API used here is cited above. The only new things are the types `light::msg::Prediction` and `light::act::ImbalancePredictor` and the light22 handler.

```cpp
// NEW: light/include/light/msg/Prediction.hpp
#pragma once
#include "actors/Message.hpp"
namespace light::msg {
  struct Prediction : public actors::MessageT<Prediction> {      // Message.hpp:198-204
    uint     sym   = 0;
    uint64_t txtim = 0;     // market time of the EOB it came from (Data.hpp:56)
    double   value = 0.0;   // e.g. L1 imbalance in [-1, 1]
    Prediction(uint s, uint64_t t, double v) : sym(s), txtim(t), value(v) {}
  };
}

// NEW: light/include/light/act/ImbalancePredictor.hpp
#pragma once
#include <cstdio>
#include <vector>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "light/msg/Prediction.hpp"
namespace light::act {
  class ImbalancePredictor : public actors::Actor {               // Actor.hpp:103
    actor_ptr ob;
    std::vector<actor_ptr> lights;
  public:
    ImbalancePredictor(actor_ptr _ob, std::vector<actor_ptr> _lights, const char* nm)
        : ob(_ob), lights(std::move(_lights)) {
      snprintf(name, sizeof(name), "%s", nm);                     // Actor::name, Actor.hpp:168
      MESSAGE_HANDLER(actors::msg::Start, start_handler);         // Actor.hpp:59
      MESSAGE_HANDLER(frame::ob::msg::EndOfBurst, eob_handler);
    }
  private:
    void start_handler(const actors::msg::Start*) noexcept {
      // same call as light22.hpp:303 / SlippageProbe.cpp:97
      ob->send(new frame::mda::msg::Subscribe(frame::mda::msg::Subscribe::HI), this);
    }
    void eob_handler(const frame::ob::msg::EndOfBurst* m) noexcept {
      const auto& pl = m->payload;                                // EndOfBurst.hpp:21
      if (!pl || pl->point_.baddata || pl->recovery) return;      // as light22.hpp:809-823
      const double b = pl->point_.bid_sz[0], a = pl->point_.ask_sz[0];  // point.hpp:164-168
      if (b + a <= 0) return;
      const double imb = (b - a) / (b + a);
      for (auto l : lights)                                       // one message per recipient (Actor.cpp:62)
        l->send(new light::msg::Prediction(pl->sym, pl->txtim_epoch, imb), this);
      // alternative, synchronous (Actor.cpp:119-143; caller owns msg, cf. light22_base.hpp:359-360):
      //   light::msg::Prediction p(pl->sym, pl->txtim_epoch, imb); l->fast_send(&p, this);
    }
  };
}

// NEW, inside light22_base<Derived,Side> (light22_base.hpp):
//   ctor, next to the existing MESSAGE_HANDLERs at :325-338:
//     MESSAGE_HANDLER(msg::Prediction, prediction_handler);
//   members:
//     double last_pred = 0.0; uint64_t last_pred_tx = 0;
//     void prediction_handler(const msg::Prediction* p) noexcept {
//       if (p->sym != uint(ssym.get())) return;   // ssym = asset id, light22_base.hpp:320
//       last_pred = p->value; last_pred_tx = p->txtim;
//     }
//   then use last_pred as an extra gate in light22::place_if_can_impl (light22.hpp:333).

// NEW wiring, sim (SimKaspr::create_lights, before create_bfa()):
//   auto pred = new light::act::ImbalancePredictor(ob, lights_for_this_book, ("PRED_" + a->name).c_str());
//   group_->add(pred);                            // Group.cpp:43-50; name must be unique (Manager.cpp:269)
// NEW wiring, kaspr (inside create_lights_for_ob, kaspr.cpp:383-437):
//   add_to_manage_q(new light::act::ImbalancePredictor(ob, these_8_lights, ("PRED_" + a->name).c_str()));
```

## What does not exist (honest gaps)

- There is no generic topic or subscription bus. Each book holds its own subscriber vectors (OB.hpp:118-119; TachBook.hpp:177), and a strategy has to know the book's pointer.
- There is no prediction or signal message, and light22 has no handler for one. OB's `has_pred` is dead code (§3b).
- `Set(TARGET_POS)` is the only existing numeric input to a light, and it sets a position target, not a signal (§4).
- TachBook's name gate rejects arbitrarily named subscribers (§3c). In kaspr, TachBook gets data only in a mode that starves OB and the lights (kaspr.cpp:488-507). In practice a predictor in this tree subscribes to **OB**.
- Determinism holds only in the single-Group sim. kaspr is one thread per actor (§5).

## Doc and code discrepancies I hit (cited)

- ACTORS_INVENTORY.md:23 lists `light/include/light/act/TachBook.hpp`. That directory contains only `light22.hpp` and `light22_base.hpp`.
- ACTORS_INVENTORY.md:15 calls OB an "MBO order book simulator". CLAUDE.md and STRATEGY_SIMULATOR_GUIDE.md:206-207 call it MBP.
- The message-id table in CLAUDE_AGENT_GUIDE.md:326-338 (">= 100 User-defined") does not match Message.hpp:130 and 142-149 (Message_N < 512, MessageT ≥ 512).
- STRATEGY_SIMULATOR_GUIDE.md:193 says a strategy "receives `EndOfBurst` messages from OB". That is true only after the strategy sends `Subscribe(HI)` (OB.cpp:1434-1441, 3078-3082).

---

## Summary

- An order-book model actor is an ordinary `actors::Actor`. It registers `Start` and `EndOfBurst` with `MESSAGE_HANDLER`, subscribes by sending `frame::mda::msg::Subscribe(HI)` to an **OB** in its Start handler (the same call light22 and SlippageProbe make), reads the 16-level `point_` arrays off the shared payload, and sends a **new** `MessageT` prediction message to each light.
- The parts that do not exist are the prediction message type, a handler and a decision gate in `light22_base`/`light22`, and the wiring lines in `SimKaspr.cpp` (`group_->add`, before BFA) and `kaspr.cpp` (`add_to_manage_q`).
- There is no generic subscription bus and light22 has no external-signal hook.
- TachBook admits only subscribers named with "aggr" or "perf" for HI, and kaspr feeds TachBook only in a mode that starves the OBs.
- In the single-Group sim, delivery is FIFO on one thread and data-driven, so it is deterministic. A `send`-based prediction for burst t reaches the light after the light has already acted on `EOB_t` and before `EOB_{t+1}`. A `fast_send` from a predictor that subscribed earlier reaches the light before its own `EOB_t`. Both orderings are DERIVED from code and untested.
- In kaspr, every actor has its own thread and that ordering is not deterministic.
