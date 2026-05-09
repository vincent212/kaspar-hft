<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# File Structure — m2_kaspar

## Overview

m2_kaspar is the standalone build tree for the Kaspr CME futures trading system. It processes MDP3 market data for ES and NQ futures, reconstructs order books, runs a shadow execution algorithm, and routes orders through a simulated or live (iLink) execution path.

## Lines of Code by Directory

| Directory | LOC | Description |
|-----------|----:|-------------|
| ilink_v8 | 141,050 | CME iLink v8 SBE protocol headers (generated) |
| mktdata_v12 | 92,690 | CME MDP3 v12 SBE market data headers (generated) |
| actors | 15,794 | Actor framework (messaging, lifecycle, coordination) |
| frame_kaspr | 11,642 | Trading framework (OB, SOM, BFA, Timer, Console) |
| chutil | 11,584 | Core utilities (time, CSV, sockets, enums) |
| ilink | 5,225 | iLink session management (handler, receiver, arbiter) |
| mdp3 | 5,130 | MDP3 market data processing and recovery |
| light | 3,440 | Execution lights (shadow algorithm, TachBook) |
| frame_ref | 2,263 | Reference data (assets, prices, market data types) |
| oogsl | 2,087 | GSL math wrappers (stats, matrix, random) |
| kaspr | 1,878 | Main application (startup, wiring, config) |
| logger | 1,088 | Logging actor |
| interface | 1,023 | Factory function headers for actor creation |
| mcast_recv | 759 | Multicast UDP receiver and PCAP reader |
| mtd | 519 | Monitoring and console display |
| mq0 | 444 | ZMQ console server |
| positionman | 321 | Per-instrument position tracking |
| db | 276 | Database actor (stubbed) |
| mk_kaspr | — | Build system templates (Makefiles only) |

---

## Directory Structure

### `kaspr/` — Main Application

The entry point. Wires all actors together and starts the system.

| File | Description |
|------|-------------|
| `src/kaspr.cpp` | Main application — creates actors, configures channels, starts event loop |
| `src/kaspr.hpp` | Kaspr class declaration — actor references, factory method declarations |
| `src/Makefile` | Builds the `kaspr` executable |
| `src/remote_messages.hpp` | Remote messaging definitions for ZMQ-based external strategies |
| `src/clean.sh` | Clean build artifacts |
| `start.sh` | Shell script to launch kaspr |
| `STRATEGY_SIMULATOR_GUIDE.md` | Developer guide for simulation, paper trading, and live trading modes |
| `messages.schema.json` | JSON schema for remote actor messages |
| `config/kaspr.ini` | Main config — channels, universe, MQ0 port |
| `config/cme.ini` | CME multicast addresses and ports per channel |
| `config/light.ini` | Shadow light parameters (nlevels, ord_sz, throttle) |
| `config/som.ini` | SOM instrument configuration |
| `config/universe.csv` | Instrument definitions (symbol, mnemonic, exchange) |
| `test/test_strategy.py` | Python strategy test harness |

---

### `actors/` — Actor Framework

Lock-free actor system with message passing, lifecycle management, remote messaging via ZMQ, and multi-process coordination.

#### `actors/cpp/` — Core Framework

| File | Description |
|------|-------------|
| `Actor.cpp` | Actor base class — message loop, queue processing |
| `Manager.cpp` | Actor lifecycle — init, start, shutdown sequencing |
| `Group.cpp` | Actor grouping for bulk start/stop |
| `RustActorRefStub.cpp` | Stub for Rust interop |
| `Makefile` | Builds libactors.a |
| `CMakeLists.txt` | CMake build (alternative to Make) |
| `build.sh` | Build helper script |

#### `actors/cpp/include/actors/` — Core Headers

| File | Description |
|------|-------------|
| `Actor.hpp` | Actor base class — message handlers, send/subscribe |
| `ActorRef.hpp` | Lightweight actor reference (address + send) |
| `Message.hpp` | Base message type with routing metadata |
| `Queue.hpp` | Lock-free SPSC message queue |
| `BQueue.hpp` | Bounded queue variant |
| `HybridBuffer.hpp` | Hybrid stack/heap buffer for messages |
| `HybridCB.hpp` | Hybrid circular buffer |
| `MemoryPool.hpp` | Pool allocator for message objects |

#### `actors/cpp/include/actors/act/` — Core Actors

| File | Description |
|------|-------------|
| `Manager.hpp` | Actor manager — creates, starts, stops actors in order |
| `Group.hpp` | Actor group — bulk lifecycle operations |
| `Timer.hpp` | Timer actor — schedules timeouts and alarms |

#### `actors/cpp/include/actors/msg/` — System Messages

| File | Description |
|------|-------------|
| `Start.hpp` | Start message — sent to actors on startup |
| `Shutdown.hpp` | Shutdown message — graceful actor teardown |
| `ShutdownThisActor.hpp` | Self-shutdown request |
| `Subscribe.hpp` | Subscribe to another actor's output |
| `Timeout.hpp` | Timer expiry notification |
| `Set.hpp` | Generic key-value configuration message |
| `Continue.hpp` | Resume processing after pause |
| `AddActor.hpp` | Dynamic actor registration |
| `ActorRemoved.hpp` | Actor removal notification |

#### `actors/cpp/include/actors/console/` — Console I/O

| File | Description |
|------|-------------|
| `ConsoleActor.hpp` | stdin/stdout console actor |
| `MQ0ServerActor.hpp` | ZMQ-based console server actor |
| `ConsoleMessages.hpp` | Console message types |
| `Table.hpp` | ASCII table formatter for console output |

#### `actors/cpp/include/actors/remote/` — Remote Messaging (ZMQ)

| File | Description |
|------|-------------|
| `ZmqSender.hpp` | ZMQ outgoing message transport |
| `ZmqReceiver.hpp` | ZMQ incoming message routing + RemoteReplyProxy |
| `Serialization.hpp` | Message serialization for ZMQ transport |
| `Reject.hpp` | Remote message rejection |

#### `actors/cpp/include/actors/registry/` — Actor Registry

| File | Description |
|------|-------------|
| `RegistryActor.hpp` | Actor name → address registry (server) |
| `RegistryClient.hpp` | Registry client for lookups |
| `RegistryQueryActor.hpp` | Registry query actor |
| `RegistryMessages.hpp` | Registry protocol messages |

#### `actors/cpp/include/actors/coordination/` — Multi-Process Coordination

| File | Description |
|------|-------------|
| `CoordinatorActor.hpp` | Bridges C++ actors with external processes |
| `MonitorActor.hpp` | Actor health monitoring |
| `GroupManager.hpp` | Manages groups of coordinated actors |
| `ZmqRouterSender.hpp` | ZMQ ROUTER pattern sender |
| `ZmqRouterReceiver.hpp` | ZMQ ROUTER pattern receiver |
| `CoordinationActorMessages.hpp` | Coordination protocol messages |
| `CoordinatorMessages.hpp` | Coordinator-specific messages |
| `messages.hpp` | Shared coordination message types |

#### `actors/cpp/src/` — Implementation Files

| Directory | Contents |
|-----------|----------|
| `console/` | ConsoleActor.cpp, MQ0ServerActor.cpp, Table.cpp |
| `coordination/` | CoordinatorActor.cpp, GroupManager.cpp, ZmqRouterReceiver.cpp, ZmqRouterSender.cpp |
| `registry/` | RegistryActor.cpp, RegistryClient.cpp, RegistryQueryActor.cpp |

#### `actors/cpp/tests/` — Unit Tests

| Directory | Contents |
|-----------|----------|
| `coordination/` | Tests for coordinated groups, group manager, messages, permission timeout |
| `registry/` | Tests for global registry |
| `remote/` | Tests for ZMQ sender |
| (root) | test_message.cpp, test_queue.cpp, test_registry_messages.cpp |

#### `actors/generated/cpp/` — Generated Code

| File | Description |
|------|-------------|
| `RemoteMessages.hpp` | Generated C++ remote message types |
| `kasprowy_messages.hpp` | Generated Kaspr-specific message types |

---

### `frame_kaspr/` — Trading Framework

Core trading actors: order book, SOM, BFA, timer, console.

#### `frame_kaspr/include/frame/ob/` — Order Book

| File | Description |
|------|-------------|
| `act/OB.hpp` | MBP (market-by-price) order book actor — receives MDP3 data, publishes BBBO/EOB |
| `act/OB_Abstract.hpp` | Abstract order book base class |
| `act/TachBook.hpp` | MBO (market-by-order) L3 order book for live trading (USE_TACHBOOK) |
| `Order.hpp` | Order structure (price, size, side, order ID) |
| `OrderQ.hpp` | Order queue — FIFO price-time priority |
| `OrderQNode.hpp` | Order queue node |
| `pub/nlevels.hpp` | N-level book snapshot publisher |

#### `frame_kaspr/include/frame/ob/msg/` — Order Book Messages

| File | Description |
|------|-------------|
| `BBBOChg.hpp` | Best bid/best offer change notification |
| `BBBOSub.hpp` | Subscribe to BBBO updates |
| `EndOfBurst.hpp` | End-of-burst notification (one per MDP3 incremental cycle) |
| `EndOfBurst2.hpp` | Extended end-of-burst with additional fields |
| `TradeNotify.hpp` | Trade execution notification |
| `CancNotify.hpp` | Order cancellation notification |
| `GapDected.hpp` | Sequence gap detected |
| `CheckBook.hpp` | Book integrity check request |
| `CheckRes.hpp` | Book check result |
| `CheckSim.hpp` | Simulation check message |
| `Clear.hpp` | Clear order book |

#### `frame_kaspr/include/frame/som/` — Simulated Order Manager

| File | Description |
|------|-------------|
| `act/SOM.hpp` | SOM actor — matches orders against OB in sim mode, routes to iLink in live mode |
| `msg/Order.hpp` | New order message |
| `msg/Cancel.hpp` | Cancel order message |
| `msg/Fill.hpp` | Fill notification |
| `msg/Ack.hpp` | Order acknowledgment |
| `msg/Reject.hpp` | Order rejection |
| `msg/CancAck.hpp` | Cancel acknowledgment |
| `msg/CancReject.hpp` | Cancel rejection |
| `msg/FillSub.hpp` | Subscribe to fills |
| `msg/GetPosition.hpp` | Position query |
| `msg/UpdatePosition.hpp` | Position update |
| `msg/ResetPositions.hpp` | Reset all positions |
| `msg/PositionResponse.hpp` | Position query response |
| `msg/ExitPos.hpp` | Exit position request |
| `msg/GetPNL.hpp` | PNL query |
| `msg/PNL.hpp` | PNL response |
| `msg/RiskInfo.hpp` | Risk information |
| `msg/SOMAction.hpp` | SOM action command |
| `msg/SetExchManager.hpp` | Set exchange manager (iLink) |
| `msg/Start.hpp` | SOM start message |
| `msg/Stop.hpp` | SOM stop message |
| `msg/UnStash.hpp` | Unstash pending orders |

#### `frame_kaspr/include/frame/mda/` — Market Data Adapter

| File | Description |
|------|-------------|
| `act/BFA.hpp` | Binary Feed Adapter — routes MDP3 decoded data to OB actors |
| `act/BinRecorder.hpp` | Binary data recorder — writes raw market data to .bin files |
| `msg/Subscribe.hpp` | Market data subscribe |

#### `frame_kaspr/include/frame/cons/` — Console

| File | Description |
|------|-------------|
| `act/Cons.hpp` | Console command handler actor |
| `msg/Cmd.hpp` | Console command message |
| `msg/CmdR.hpp` | Console command response |
| `msg/Get.hpp` | Console get request |
| `msg/Page.hpp` | Paged console output |

#### `frame_kaspr/include/frame/mtim/` — Timer

| File | Description |
|------|-------------|
| `act/Timer.hpp` | System timer actor for scheduled events |
| `msg/Alarm.hpp` | Alarm message (absolute time trigger) |
| `msg/AlarmClockSub.hpp` | Alarm clock subscription |
| `msg/TimeOutSub.hpp` | Timeout subscription (relative delay) |

#### `frame_kaspr/include/frame/` — Other

| File | Description |
|------|-------------|
| `pos/Position.hpp` | Position data structure |

#### `frame_kaspr/src/` — Implementation

| File | Description |
|------|-------------|
| `OB.cpp` | Order book implementation — price level management, matching |
| `SOM.cpp` | SOM implementation — order matching, fill generation, position tracking |
| `BFA_if.cpp` | BFA factory function |
| `OB_if.cpp` | OB factory function |
| `SOM_if.cpp` | SOM factory function |
| `Cons_if.cpp` | Console factory function |
| `Timer.cpp` | Timer implementation |
| `Timer_if.cpp` | Timer factory function |
| `BinRecorder_if.cpp` | BinRecorder factory function |
| `RefData.cpp` | Reference data loading (universe.csv parser) |
| `point.cpp` | Price point utilities |
| `Makefile` | Builds libframe.a |

---

### `mdp3/` — MDP3 Market Data Processing

Decodes CME MDP3 SBE messages, handles recovery, routes to order books.

#### `mdp3/include/mdp3/`

| File | Description |
|------|-------------|
| `handler_if.hpp` | Feed handler template — routes decoded MDP3 to MBO/MBP order books |
| `mbo_if.hpp` | MBO (market-by-order) handler interface |
| `DataDecoder.hpp` | SBE message decoder — maps binary MDP3 to handler callbacks |
| `msg_decoder.hpp` | Message-level decoder utilities |
| `act/MessageProcessor.hpp` | Processes MDP3 incremental messages from multicast |
| `act/RecoveryProcessor.hpp` | Handles MDP3 snapshot recovery on gap |
| `act/PCAPReader.hpp` | Reads PCAP capture files for replay |
| `act/InstrumentRecoveryRecorder.hpp` | Records instrument definition snapshots |
| `act/DataRecoveryRecorder.hpp` | Records data recovery snapshots |

#### `mdp3/include/mdp3/msg/` — MDP3 Messages

| File | Description |
|------|-------------|
| `DoDataRecovery.hpp` | Trigger data recovery |
| `DoInstrumentRecovery.hpp` | Trigger instrument recovery |
| `EndDataRecovery.hpp` | Data recovery complete |
| `EndInstrumentRecovery.hpp` | Instrument recovery complete |
| `StartQ.hpp` | Start message queue |
| `StopQ.hpp` | Stop message queue |

#### `mdp3/src/`

| File | Description |
|------|-------------|
| `message_processor.cpp` | MessageProcessor implementation and factory |
| `recoveryprocessor.cpp` | RecoveryProcessor implementation and factory |
| `socketreader.cpp` | MDP3 socket reader factory |
| `msgbuf.cpp` | Message buffer factory |
| `instrumentrecorder.cpp` | Instrument recovery recorder factory |
| `recoveryrecorder.cpp` | Data recovery recorder factory |
| `Makefile` | Builds libmdp3.a |

---

### `light/` — Execution Lights

Shadow execution algorithm — places orders by piggy-backing on real market activity.

#### `light/include/light/act/`

| File | Description |
|------|-------------|
| `light22.hpp` | Shadow light actor — placement/cancellation logic per instrument per side |
| `light22_base.hpp` | Base class for light22 (shared state, configuration) |
| `TachBook.hpp` | Light-variant TachBook — MBO L3 book for live trading |

#### `light/include/light/msg/`

| File | Description |
|------|-------------|
| `GetLightInfo.hpp` | Request light status information |
| `LightInfo.hpp` | Light status response (working orders, position) |
| `LightSetOffTR.hpp` | Set off-the-run flag |
| `PositionInfo.hpp` | Position information message |
| `RegisterLight.hpp` | Register light with coordinator |
| `Set.hpp` | Light configuration message |
| `Start.hpp` | Start light |
| `Stop.hpp` | Stop light |

#### `light/include/light/`

| File | Description |
|------|-------------|
| `qcoord.hpp` | QCoord — shared working order accounting across lights per instrument |

#### `light/src/`

| File | Description |
|------|-------------|
| `light22.cpp` | light22 implementation and factory |
| `TachBook.cpp` | TachBook implementation and factory |
| `PCoord.cpp` | PCoord — shared position state across lights per instrument |
| `QCoord.cpp` | QCoord implementation |
| `Makefile` | Builds liblight.a |

---

### `mcast_recv/` — Multicast Receiver

Reads CME multicast UDP packets or PCAP files and buffers them for MDP3 processing.

| File | Description |
|------|-------------|
| `include/mcast_recv/act/SocketReader.hpp` | Multicast UDP socket reader actor |
| `include/mcast_recv/act/MsgBuf.hpp` | Message buffer between socket reader and MDP3 processor |
| `include/mcast_recv/act/PCAPReader.hpp` | PCAP file reader actor |
| `include/mcast_recv/message_buffer.hpp` | Message buffer data structure |
| `include/mcast_recv/msg/ProcessQ.hpp` | Process queue message |
| `src/SocketReader.cpp` | Socket reader implementation |
| `src/MsgBuf.cpp` | Message buffer implementation |
| `src/Makefile` | Builds libmcast_recv.a |

---

### `ilink/` — iLink Session Handler

CME iLink v3 session management for live order execution.

| File | Description |
|------|-------------|
| `include/ilink/act/ILinkHandler.hpp` | iLink session handler actor — connect, negotiate, establish |
| `include/ilink/act/ILinkRec.hpp` | iLink message receiver actor |
| `include/ilink/act/ILinkArbiter.hpp` | Primary/secondary session arbitration |
| `include/ilink/ILinkCBIF.hpp` | iLink callback interface |
| `include/ilink/ILinkCBImp.hpp` | iLink callback implementation |
| `include/ilink/ILinkRcv.hpp` | iLink receive logic |
| `include/ilink/ILinkSnd.hpp` | iLink send logic |
| `include/ilink/audit.hpp` | iLink audit trail |
| `include/ilink/ilink_null.hpp` | Null iLink (no-op for simulation) |
| `include/ilink/sign.hpp` | HMAC signing for iLink authentication |
| `include/ilink/sock_help.hpp` | Socket helpers for iLink connections |
| `include/ilink/msg/` | iLink message types (30 messages: Negotiate, Establish, ExecutionReport, etc.) |
| `src/ilink_handler.cpp` | ILinkHandler factory |
| `src/ilink_rec.cpp` | ILinkReceiver factory |
| `src/ilink_arbiter.cpp` | ILinkArbiter factory |
| `src/Makefile` | Builds libilink.a |

---

### `ilink_v8/` — iLink v8 SBE Headers (Generated)

141,050 lines of auto-generated C++ headers from the CME iLink v8 SBE schema. Each file defines one SBE message or type (e.g., `NewOrderSingle514.h`, `ExecutionReportNew522.h`). Not hand-edited.

---

### `mktdata_v12/` — MDP3 v12 SBE Headers (Generated)

92,690 lines of auto-generated C++ headers from the CME MDP3 v12 SBE schema. Defines market data messages (e.g., `MDIncrementalRefreshBook46.h`, `MDIncrementalRefreshOrderBook47.h`, `SnapshotFullRefresh52.h`). Not hand-edited.

---

### `db/` — Database Actor

Stubbed database actor. Subscribes to OB and SOM events but handlers are no-ops.

| File | Description |
|------|-------------|
| `include/db/act/DB.hpp` | DB actor — stubbed message handlers for BBBO, fills, positions |
| `include/db/msg/AddOTRFillRecord.hpp` | OTR fill record message |
| `include/db/msg/AddTradeRecord.hpp` | Trade record message |
| `src/DB.cpp` | DB factory function (create_DB) |
| `src/Makefile` | Builds libdb.a |

---

### `mtd/` — Monitoring

Console display actor — handles `prices`, `bbbo`, `assets`, `fills`, `get_orders` commands.

| File | Description |
|------|-------------|
| `include/mtd/act/MTD.hpp` | MTD actor — console command dispatch, BBBO display |
| `src/MTD.cpp` | MTD factory function (create_MTD) |
| `src/Makefile` | Builds libmtd.a |

---

### `mq0/` — ZMQ Console Server

External console access via ZMQ (default port 7777).

| File | Description |
|------|-------------|
| `include/mq0/act/MQ0_server.hpp` | MQ0 server actor — ZMQ REP socket for console commands |
| `include/mq0/msg/MQ0_Msg.hpp` | MQ0 request message |
| `include/mq0/msg/MQ0_Rep.hpp` | MQ0 reply message |
| `src/MQ0_server.cpp` | MQ0 server factory |
| `src/Makefile` | Builds libmq0.a |

---

### `logger/` — Logging Actor

| File | Description |
|------|-------------|
| `include/logger/act/Logger.hpp` | Logger actor — receives log messages, writes to file/stdout |
| `include/logger/msg/Log.hpp` | Log message type |
| `include/logger/msg/CMEAudit.hpp` | CME audit trail log message |
| `src/Logger.cpp` | Logger implementation |
| `src/Makefile` | Builds liblogger.a |

---

### `positionman/` — Position Manager

| File | Description |
|------|-------------|
| `include/positionman/act/PositionManager.hpp` | Aggregates positions across all instruments |
| `include/positionman/msg/AddToPos.hpp` | Increment/decrement position |
| `include/positionman/msg/GetPos.hpp` | Position query |
| `include/positionman/msg/Pos.hpp` | Position reply |
| `src/PositionManager.cpp` | PositionManager factory |
| `src/Makefile` | Builds libpositionman.a |

---

### `chutil/` — Core Utilities

Shared utility library used across all components.

#### `chutil/include/chutil/`

| File | Description |
|------|-------------|
| `Time.hpp` | Date/time class with nanosecond precision, epoch conversion |
| `CSVFileReader.hpp` | CSV file parser with numeric type conversion |
| `Assert.hpp` | Custom assertion macros with debug output |
| `Factory.hpp` | Object pool allocator (used by PooledObject) |
| `FileSystem.hpp` | File system helpers |
| `Macros.hpp` | Common preprocessor macros |
| `PooledObject.hpp` | Object pool for reuse |
| `Table.hpp` | Table data structure |
| `accumulators.hpp` | Statistical accumulators |
| `cache_array.hpp` | Cache-friendly array |
| `csv.h` | CSV header utilities |
| `hash_map.hpp` | Hash map wrapper |
| `hash_set.hpp` | Hash set wrapper |
| `places.hpp` | Place value utilities |
| `price_convert.hpp` | Decimal-to-32nds price conversion |
| `shared_ptr_u.h` | Shared pointer utility |
| `sig_hand.hpp` | Signal handler — stack traces on crash |
| `sock_help.hpp` | Socket utility functions |
| `udp_socket.hpp` | UDP multicast socket operations |
| `uniform_dist.hpp` | Uniform distribution |
| `ut.hpp` | Unit test helpers |

#### `chutil/include/bfile/`

| File | Description |
|------|-------------|
| `r_l3.hpp` | Binary L3 record types — structs for MBP, MBO, trade, GAP, CHR messages |

#### `chutil/include/enum/`

| File | Description |
|------|-------------|
| `e_names.hpp` | Master enum namespace (includes all enums) |
| `buy_sell.hpp` | Buy/sell enum |
| `exch_code.hpp` | Exchange code enum (en::x — CMEMDFUT, etc.) |
| `l3.hpp` | L3 message type enum (MBP, MBO, MBPT, GAP, CHR, etc.) |
| `light_ev.hpp` | Light event type enum |
| `som_code.hpp` | SOM status codes |
| `order_type.hpp` | Order type enum (limit, market, etc.) |
| `alpha.hpp` | Alpha signal enum |
| `cal.hpp` | Calendar enum |
| `interval.hpp` | Time interval enum |
| `mat.hpp` | Maturity enum |
| `mod_typ.hpp` | Model type enum |
| `models.hpp` | Models enum |
| `msg_code.hpp` | Message code enum |
| `pos.hpp` | Position enum |
| `rv_freq.hpp` | Realized volatility frequency enum |
| `th.hpp` | Theta enum |
| `trader.hpp` | Trader ID enum |

#### `chutil/gencode/enum/`

| File | Description |
|------|-------------|
| `genenum.py` | Python script to generate enum .hpp files from .enum definitions |
| `def/*.enum` | Enum definition files (one per enum type) |

#### `chutil/src/`

| File | Description |
|------|-------------|
| `CSVFileReader.cpp` | CSV reader implementation |
| `Time.cpp` | Time class implementation |
| `theta.cpp` | Theta calculation |
| `uniform_dist.cpp` | Uniform distribution implementation |
| `Makefile` | Builds libchutil.a |

---

### `frame_ref/` — Reference Data Framework

Shared data types for market data and reference data.

| File | Description |
|------|-------------|
| `include/frame/ref/Asset.hpp` | Asset definition (symbol, mnemonic, tick size, multiplier) |
| `include/frame/ref/Price.hpp` | Price type with display factor conversion |
| `include/frame/ref/RefData.hpp` | Reference data container — instruments, channels, security IDs |
| `include/frame/mda/OrderID.hpp` | Order ID type |
| `include/frame/mda/msg/Data.hpp` | Market data message — wraps L3 binary records |
| `include/frame/mda/msg/point.hpp` | Price point message |

---

### `oogsl/` — GSL Math Wrappers

Object-oriented wrappers around GNU Scientific Library.

| File | Description |
|------|-------------|
| `include/oogsl/Stats.hpp` | Statistics — percentile, regression, correlation, risk metrics |
| `include/oogsl/matrix.hpp` | GSL matrix class with transpose, append, conversions |
| `include/oogsl/gvector.hpp` | GSL vector class |
| `include/oogsl/func.h` | GSL function wrappers |
| `include/oogsl/permutation.hpp` | GSL permutation |
| `include/oogsl/random.hpp` | GSL random number generator |
| `src/Stats.cpp` | Stats implementation |
| `src/random.cpp` | Random implementation |
| `src/Makefile` | Builds liboogsl.a |

---

### `interface/` — Factory Function Headers

Declares `create_*` factory functions for each actor. Separates actor creation interface from implementation so `kaspr.cpp` only needs the interface headers.

| Path | Description |
|------|-------------|
| `db/if/DB.hpp` | `create_DB()` — database actor |
| `mtd/if/MTD.hpp` | `create_MTD()` — monitoring actor |
| `mq0/if/MQ0_server.hpp` | `create_MQ0_server()` — ZMQ console server |
| `cons/if/Cons.hpp` | `create_Cons()` — console handler |
| `positionman/if/PositionManager.hpp` | `create_PositionManager()` |
| `mda/if/BFA.hpp` | `create_BFA()` — binary feed adapter |
| `mdp3/if/MessageProcessor.hpp` | `create_MessageProcessor()` — MDP3 processor |
| `mdp3/if/RecoveryProcessor.hpp` | `create_RecoveryProcessor()` — MDP3 recovery |
| `mdp3/if/SocketReader.hpp` | `create_SocketReader()` |
| `mdp3/if/MsgBuf.hpp` | `create_MsgBuf()` |
| `mdp3/if/mdp3.hpp` | MDP3 utility interface (includes mcast_recv interfaces) |
| `mdp3/if/DataRecoveryRecorder.hpp` | `create_DataRecoveryRecorder()` |
| `mdp3/if/InstrumentRecoveryRecorder.hpp` | `create_InstrumentRecoveryRecorder()` |
| `mcast_recv/if/SocketReader.hpp` | `create_SocketReader()` — multicast |
| `mcast_recv/if/MsgBuf.hpp` | `create_MsgBuf()` — multicast buffer |
| `mcast_recv/if/PCAPReader.hpp` | `create_PCAPReader()` — PCAP |
| `ilink/if/ILinkHandler.hpp` | `create_ILinkHandler()` |
| `ilink/if/ILinkRec.hpp` | `create_ILinkReceiver()` |
| `ilink/if/ILinkArbiter.hpp` | `create_ILinkArbiter()` |
| `light/if/light22.hpp` | `create_light22()` — shadow light |
| `light/if/TachBook.hpp` | `create_TachBook()` |
| `light/if/PCoord.hpp` | `create_PCoord()` — position coordinator |
| `light/if/QCoord.hpp` | `create_QCoord()` — queue coordinator |
| `som/if/SOM.hpp` | `create_SOM()` |
| `frame/ob/if/OB.hpp` | `create_OB()` — order book |
| `frame/som/if/SOM.hpp` | `create_SOM()` (frame variant) |
| `frame/cons/if/Cons.hpp` | `create_Cons()` (frame variant) |
| `frame/mtim/if/Timer.hpp` | `create_Timer()` |
| `frame/mda/if/BinRecorder.hpp` | `create_BinRecorder()` |
| `frame/mtd/if/MTD.hpp` | `create_MTD()` (frame variant) |

---

### `setclassid/` — Message ID Uniqueness Checker

Scans actor message types to ensure unique IDs across the codebase (avoids handler dispatch collisions).

| File | Description |
|------|-------------|
| `setclassid.py` | Scans headers for `Message_N<ID>` and checks for ID collisions |
| `README.md` | Usage guide |

---

### `genconfig/` — Configuration Generator

Generates CME multicast and iLink configuration files from CME's published XML config.

| File | Description |
|------|-------------|
| `genconfig.sh` | Main script — runs Python generators |
| `genconfig_mdp3_3.py` | Generates MDP3 multicast config (channels, IPs, ports) |
| `genconfig_ilink_3.py` | Generates iLink session config |
| `README.md` | Usage guide |

---

### `mk_kaspr/` — Build System

Makefile templates and build infrastructure.

| File | Description |
|------|-------------|
| `glob_begin.mk` | Global build settings — compiler flags, include paths, platform detection |
| `lib_template.mk` | Template Makefile for building static libraries (.a) |
| `app_template.mk` | Template Makefile for building executables |
| `MAINSETUP.md` | Build system setup guide |

---

### Root Files

| File | Description |
|------|-------------|
| `Makefile` | Top-level Makefile — builds all libraries then kaspr executable |
| `CLAUDE.md` | AI agent instructions for working with this codebase |
| `ACTORS_INVENTORY.md` | Complete actor inventory with headers and purposes |
| `STRATEGY_SIMULATOR_GUIDE.md` | Developer guide for three operating modes |
| `FILE_STRUCTURE.md` | This file |
| `.gitignore` | Git ignore rules |
