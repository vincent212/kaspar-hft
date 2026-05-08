<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Actors Inventory — m2_kaspar

## Kaspr Runtime Actors

These actors are instantiated by `kaspr.cpp` during startup.

| Actor | Header | Library | Purpose |
|-------|--------|---------|---------|
| OB | `frame_kaspr/include/frame/ob/act/OB.hpp` | libframe | MBO order book simulator (per instrument) |
| TachBook | `frame_kaspr/include/frame/ob/act/TachBook.hpp` | libframe | MBO L3 order book (USE_TACHBOOK) |
| Timer | `frame_kaspr/include/frame/mtim/act/Timer.hpp` | libframe | System timer for scheduled events |
| SOM | `frame_kaspr/include/frame/som/act/SOM.hpp` | libframe | Simulated Order Manager |
| Cons | `frame_kaspr/include/frame/cons/act/Cons.hpp` | libframe | Console command handler |
| BFA | `frame_kaspr/include/frame/mda/act/BFA.hpp` | libframe | Binary Feed Adapter (MDP3 → OB) |
| BinRecorder | `frame_kaspr/include/frame/mda/act/BinRecorder.hpp` | libframe | Binary data recorder |
| light22 | `light/include/light/act/light22.hpp` | liblight | Order execution |
| TachBook (light) | `light/include/light/act/TachBook.hpp` | liblight | MBO L3 book (USE_TACHBOOK, light variant) |
| DB | `db/include/db/act/DB.hpp` | libdb | Database actor (BBO writes, fill logging) |
| MTD | `mtd/include/mtd/act/MTD.hpp` | libmtd | Monitoring (console commands, BBBO display) |
| MQ0_server | `mq0/include/mq0/act/MQ0_server.hpp` | libmq0 | ZMQ server for external monitoring |
| PositionManager | `positionman/include/positionman/act/PositionManager.hpp` | libpositionman | Position tracking per instrument |
| Logger | `logger/include/logger/act/Logger.hpp` | liblogger | Logging actor |
| ILinkHandler | `ilink/include/ilink/act/ILinkHandler.hpp` | libilink | iLink CME session handler |
| ILinkReceiver | `ilink/include/ilink/act/ILinkRec.hpp` | libilink | iLink message receiver |
| ILinkArbiter | `ilink/include/ilink/act/ILinkArbiter.hpp` | libilink | iLink primary/secondary arbitration |

## MDP3 Actors (Market Data)

| Actor | Header | Library | Purpose |
|-------|--------|---------|---------|
| MessageProcessor | `mdp3/include/mdp3/act/MessageProcessor.hpp` | libmdp3 | Processes MDP3 incremental messages |
| RecoveryProcessor | `mdp3/include/mdp3/act/RecoveryProcessor.hpp` | libmdp3 | Handles MDP3 snapshot recovery |
| PCAPReader | `mdp3/include/mdp3/act/PCAPReader.hpp` | libmdp3 | Reads PCAP capture files |
| InstrumentRecoveryRecorder | `mdp3/include/mdp3/act/InstrumentRecoveryRecorder.hpp` | libmdp3 | Records instrument recovery data |
| DataRecoveryRecorder | `mdp3/include/mdp3/act/DataRecoveryRecorder.hpp` | libmdp3 | Records data recovery snapshots |

## Multicast Actors

| Actor | Header | Library | Purpose |
|-------|--------|---------|---------|
| SocketReader | `mcast_recv/include/mcast_recv/act/SocketReader.hpp` | libmcast_recv | Reads multicast UDP packets |
| MsgBuf | `mcast_recv/include/mcast_recv/act/MsgBuf.hpp` | libmcast_recv | Message buffer between socket and processor |
| PCAPReader | `mcast_recv/include/mcast_recv/act/PCAPReader.hpp` | libmcast_recv | PCAP file reader |
| ITCHSocketReader | `mcast_recv/include/mcast_recv/act/ITCHSocketReader.hpp` | libmcast_recv | ITCH protocol socket reader |

## Actor Framework (actors/cpp)

| Actor | Header | Purpose |
|-------|--------|---------|
| Manager | `actors/cpp/include/actors/act/Manager.hpp` | Actor lifecycle management |
| Group | `actors/cpp/include/actors/act/Group.hpp` | Actor grouping and bulk operations |
| ZmqSender | `actors/cpp/include/actors/remote/ZmqSender.hpp` | ZMQ outgoing message transport |
| ZmqReceiver | `actors/cpp/include/actors/remote/ZmqReceiver.hpp` | ZMQ incoming message routing |
| RemoteReplyProxy | `actors/cpp/include/actors/remote/ZmqReceiver.hpp` | Proxy for remote actor replies |
| RegistryActor | `actors/cpp/include/actors/registry/RegistryActor.hpp` | Actor name → address registry |
| RegistryQueryActor | `actors/cpp/include/actors/registry/RegistryQueryActor.hpp` | Registry lookup queries |
| CoordinatorActor | `actors/cpp/include/actors/coordination/CoordinatorActor.hpp` | Multi-actor coordination |
| MonitorActor | `actors/cpp/include/actors/coordination/MonitorActor.hpp` | Actor health monitoring |
| ZmqRouterSender | `actors/cpp/include/actors/coordination/ZmqRouterSender.hpp` | ZMQ ROUTER pattern sender |
| ZmqRouterReceiver | `actors/cpp/include/actors/coordination/ZmqRouterReceiver.hpp` | ZMQ ROUTER pattern receiver |
| ConsoleActor | `actors/cpp/include/actors/console/ConsoleActor.hpp` | Console I/O actor |
| MQ0ServerActor | `actors/cpp/include/actors/console/MQ0ServerActor.hpp` | ZMQ-based console server |

## README Coverage

All source directories have README.md files. Build output dirs (`lib/`, `libg/`) excluded.
