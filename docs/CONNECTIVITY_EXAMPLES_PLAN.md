<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Plan: Shrink-Wrap CME Connectivity (MDP3 + iLink 3) into Runnable Examples

## Why

Kaspar is powerful but currently ships as one large wired-together program
(`kaspr/src/kaspr.cpp`, ~500 lines) plus a framework. A newcomer who just wants
to *connect to CME, stream market data, and send an order* has to read the whole
stack. Commercial SDKs (OnixS, B2BITS) win on "out-of-the-box." The factories
already exist (`create_BFA`, `create_ILinkHandler`, `create_SOM`,
`create_MQ0_server`) and `cme.ini` already has `cert_*` vs `prod_*` environments
— what's missing is **minimal, standalone examples**, a **thin facade API** over
the actor wiring, and a **Python control plane over MQ0** so a script can drive
the whole thing.

## Goal

From a fresh clone, after filling in CME **cert** credentials, a user can in
~30 minutes:

1. stream MDP3 market data for one instrument,
2. establish an iLink 3 session,
3. send and cancel an order in cert — and
4. do all of the above **from a Python script** over MQ0 (ZMQ), without writing
   C++.

## Non-goals

- Not replacing `kaspr.cpp`; the full strategy stack stays.
- Not hiding the actor model from advanced users — the facade is opt-in.
- Not truly "zero-config": CME onboarding (firm/session IDs, access keys, IP
  allowlist, autocertification) is external. We make it a documented checklist,
  not a surprise.

---

## Architecture

Three layers, each usable on its own:

```
  Python script  ──ZMQ REQ/REP──►  MQ0_server ──►  example console actor
   (order.py)                        (per example)      │
                                                        ├─► md::Feed   (MDP3 in)
                                                        └─► oe::Session (iLink out)
```

- **Facade API** (`connect/` library) — convenience classes that hide
  Manager/Group/actor wiring:
  - `md::Feed` — wraps `create_SocketReader` + `create_BFA` + book. Usage:
    ```cpp
    md::Feed feed("config/cme.ini", "cert_equity");
    feed.subscribe("ESZ5");
    feed.on_book ([](const md::Book& b){ /* top of book */ });
    feed.on_trade([](const md::Trade& t){ /* prints */ });
    feed.run();
    ```
  - `oe::Session` — wraps `create_ILinkHandler` (+ arbiter/rec). Usage:
    ```cpp
    oe::Session s("config/ilink.cert.ini");
    s.on_ready([&]{ /* negotiate+establish done */ });
    s.on_exec ([](const oe::ExecReport& er){ /* ack/fill */ });
    s.connect();
    auto id = s.new_order("ESZ5", oe::Side::Buy, /*qty*/1, /*px*/5000.00);
    s.cancel(id);
    ```
  These are thin wrappers, not a rewrite — they call the existing factories.

- **Example programs** (`examples/connectivity/`) — one file each, header
  comment stating what it does, config needed, and expected output.

- **MQ0 control plane** — every example embeds a small **console actor** that
  registers text commands, plus `create_MQ0_server("ZMQ", port, console)`. A
  Python REQ client (pattern in `mq0/MQ0_CLIENT_GUIDE.md`) sends command strings
  and gets replies. Commands, e.g.:
  - `subscribe ESZ5` / `book ESZ5` / `unsubscribe ESZ5`
  - `new_order ESZ5 buy 1 5000.00` → returns a client order id
  - `cancel <id>` / `status` / `session` (session state)
  This is the "drive it from Python" layer the C++ examples expose for free.

---

## Deliverables

### Examples (build/run order)

1. **`md_tail`** — connect MDP3 (`cert` by default), subscribe to one
   instrument, print incremental book updates + trades. Proves market-data
   connectivity. `md::Feed` only; no order entry, no SOM, no lights.
2. **`ilink_hello`** — iLink 3 session lifecycle only: Negotiate → Establish →
   Sequence/heartbeat → Terminate, printing each transition. Proves order-entry
   auth with **zero order risk**.
3. **`ilink_send_order`** — extends #2: on ready, send one New Order Single
   (limit, far from market) to cert, print the ExecutionReport ack, then Cancel
   and print the cancel ack. The headline "it works" demo.
4. **`connect_all`** — `md_tail` + `oe::Session` together, driven by MQ0: wait
   for a book, and on a Python `new_order` command place a passive order at/through
   top-of-book in cert, print the exec report. The real shape of a trading loop.

### Python control-plane

- **`examples/python/kaspar_mq0.py`** — a tiny client library (REQ socket,
  timeouts per `MQ0_CLIENT_GUIDE.md`) exposing `subscribe()`, `book()`,
  `new_order()`, `cancel()`, `status()`.
- **`examples/python/send_order.py`** — a 15-line script: connect to a running
  `connect_all`, subscribe, read top of book, send an order, print the ack. This
  is the "no C++ required" story.

### Supporting

- **`examples/connectivity/README.md`** — per-example quickstart: build, the
  exact command, expected output, and a troubleshooting table (no data → IP
  allowlist / multicast route; Establish reject → access key / session id).
- **`config/credentials.cert.ini.example`** — one annotated template
  consolidating firm id, session id, access key/secret (HMAC), UUID, and cert
  endpoints. Real creds git-ignored. (iLink session config is currently
  scattered; consolidating it is part of this work.)
- **`docs/CME_ONBOARDING.md`** — the honest checklist: obtaining a cert session,
  autocertification, IP registration, New Release vs Cert environments.
- **Build**: `make examples` / `./build.sh examples`; each example a small
  standalone binary.
- **Safety rails**: examples default to **cert**; targeting **prod** requires an
  explicit `--prod` flag *and* an env var. `ilink_send_order`/`connect_all` print
  a loud banner naming the environment before any send.

---

## Phasing

- **P0 — design** (~1–2 days): trace the minimal MD and iLink wiring out of
  `kaspr.cpp`; define the `md::Feed` / `oe::Session` facade headers and the MQ0
  command grammar. Deliverable: header sketches + command list.
- **P1**: `md::Feed` + `md_tail` + config template + README quickstart. First
  "clone → see data" win.
- **P2**: `oe::Session` + `ilink_hello` (session, no orders).
- **P3**: `ilink_send_order` (new + cancel in cert) + `CME_ONBOARDING.md`.
- **P4**: MQ0 console for the examples + `connect_all` + the Python client
  (`kaspar_mq0.py`, `send_order.py`). This is where "send an order from Python"
  lands.
- **P5**: polish — clear error messages on the common failure modes,
  `make examples`, and an asciinema/GIF of `md_tail` and `send_order.py` for the
  README.

## Success criteria

- `./md_tail cert ESZ5` prints live cert book updates.
- `./ilink_send_order` establishes a cert session and prints an ack for a new
  order + a cancel, from a fresh clone, in < 30 min of user time (excluding CME
  onboarding).
- `python examples/python/send_order.py` sends an order to cert through a running
  `connect_all` and prints the ack — **no C++ written**.
- The examples README + onboarding doc answer "why won't it connect?" without
  reading framework code.

## Marketing tie-in

Each example doubles as content that targets the "you need a commercial SDK"
search intent, now with an artifact to back it:

- *"Connect to CME iLink 3 and send an order in 30 lines of open-source C++."*
- *"Drive a CME order-entry session from Python over ZMQ."*
