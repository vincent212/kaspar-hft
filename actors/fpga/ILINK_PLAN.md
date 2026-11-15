# Plan: iLink on the Alveo X3522PV, with FPGA actors

A separate project from the MDP3 handler ([MDP3_PLAN.md](MDP3_PLAN.md)), with its own
article.

The FPGA stays part of Kaspar. An actor on the card has handlers for place order
and cancel, and sends acks, fills and rejects to FpgaILinkHandler on the CPU, like any
actor. The card owns the TCP connection to CME, encodes and sends order messages,
and decodes the replies. Kaspar keeps the iLink session (Negotiate, Establish,
keepalive, sequence recovery), risk in SOM, and audit. **The card never sends an
order of its own:** every order is a message from the CPU.

TCP itself comes from an existing core, not from us; iLink is written as actors on
top of it. The options:

| core | what it is | notes |
|---|---|---|
| [fpga-network-stack](https://github.com/fpgasystems/fpga-network-stack) | open-source HLS TCP/IP stack, 10 to 100 Gbit/s | see below; 100G-only predecessor Limago is archived |
| [NPAP](https://www.missinglinkelectronics.com/wp-content/uploads/2026/03/MLE-NPAP-100G-on-AMD-Alveo-20260228.pdf) (Missing Link Electronics) | commercial TCP/UDP/IP stack for Alveo, source available under license | about 30k LUTs with two TCP engines |

Limago's own README now says it is no longer active and points to
[fpga-network-stack](https://github.com/fpgasystems/fpga-network-stack) (ETH Zurich
and Xilinx, last pushed 2024), so that replaces it as the open-source candidate. It
takes 8-byte data paths, so 10 GbE, and it was written for Vivado 2022.2.

First test: its TCP engine, `hls/toe`, compiled unchanged with Vitis 2026.1 for the
XCUX35 (`-2`, 64-bit path, MSS 1460, 16 sessions, window scaling off): every one of
66 pipelines at II = 1, estimated 221 MHz. C synthesis only: no simulation, no place
and route, no MAC, and its testbench has not been run.

Neither core has a latency figure here yet; that decides it.

## The design

```
Kaspar (CPU)                                   Card
light22 -> SOM (risk) -> FpgaILinkHandler --PlaceOrder, Cancel--> ILinkOrders --+--> TcpSession -> TCP core -> MAC -> CME
                         (CPU actor)  --session bytes-------> (actor)       |
                               ^                                            |
                               +---Ack, Fill, Reject, session bytes---------+  <-- CME
```

| part | where | what it does |
|---|---|---|
| **ILinkOrders** | card actor | handlers `on_place` and `on_cancel`: fills the session's constant fields, writes NewOrderSingle514 (144 bytes) or OrderCancelRequest516 (108 bytes). Decodes execution reports into Ack, Fill and Reject messages to FpgaILinkHandler. Passes any other message up as bytes |
| **TcpSession** | card actor | owns the connection through the TCP core, including TCP's own retransmits. One stream out, in order. Tells FpgaILinkHandler when the connection opens or drops |
| **FpgaILinkHandler** | CPU actor | a drop-in replacement for ILinkHandler: same messages in from SOM and the arbiter, same Ack, Fill and Reject out. Same session, sequence numbers, ClOrdID, order maps and audit, but it talks to the card over a CpuEnd link instead of a socket. SOM cannot tell which handler it has, and ILinkHandler stays for running without a card |
| **SOM, ILinkArbiter** | Kaspar, unchanged | risk, venue selection |

Generated from CME's iLink schema (`genschema/ilinkbinary.xml`), as gen_decoder.py
does for MDP3: the encoders and the execution report decoder.

**Sequence numbers stay on the CPU.** Every order comes from the CPU, so
FpgaILinkHandler keeps `NextSeqNo`, `OrderRequestID` and ClOrdID and puts them in the
PlaceOrder and Cancel messages. Keepalive and party details use the same counter
there, unchanged. Orders and session bytes share one in-order path, so messages
reach TCP in sequence order, and no state is shared with the card. Replies carry
CME's sequence number up, so FpgaILinkHandler's gap check is unchanged.

A PlaceOrder message is one envelope: price, quantity, security id, side, SeqNum,
ClOrdID, OrderRequestID. SendingTimeEpoch can be taken on the card. ExecutionMode
must be 0, not null, or CME rejects the order (`ILinkSnd.hpp`).

### What moves, from today's code

| today | after |
|---|---|
| `send_new_order_single`, `send_cancel` (`ILinkSnd.hpp`): buffer, SBE encode | ILinkOrders `on_place`, `on_cancel` |
| `sockhelp::send_message` (`sock_help.hpp`): SOFH, `send()` | ILinkOrders and TcpSession |
| `ILinkReceiver`, `recv_message`, SBE decode (`ILinkRcv.hpp`) | TcpSession and ILinkOrders |
| `ILinkHandler::executionReport_handler` turning reports into Ack, Fill, Reject | split: the decoding on the card, the order bookkeeping in FpgaILinkHandler |
| `send()` failure calls `abort()`; a failed `recv` stops silently | a connection-state message from TcpSession |
| Onload for orders | not needed for iLink |

`OrderCancelReplaceRequest515` is not reachable in the code today
(`ILinkHandler.hpp`, "cancel replace not implemented"), so it stays out.

### Open points

- **Connection loss.** FpgaILinkHandler needs a defined response to TCP dropping. Today
  there is no in-flight tracking.
- **Live wiring.** No `.cpp` in the trees searched calls `create_ILinkHandler`, so
  how production starts it was not found.
- **Latency.** No measurement of the order path exists today, so there is no number
  to beat yet. The order still crosses PCIe once.

## Phases

| phase | work | done when |
|---|---|---|
| **0. Baseline** | measure today's order path, order decision to `send()` returning | a number to beat |
| **1. TCP core** | run fpga-network-stack's testbench under Vitis 2026.1, measure latency; get a figure for NPAP; pick one | a core and a latency number |
| **2. ILinkOrders** | handlers, encoders and the execution report decoder, generated from the schema | byte-identical to `ILinkSnd` on orders; the same acks and fills as `ILinkRcv` on reports |
| **3. TcpSession** | wrap the core as an actor, against a CPU peer in simulation | a connection opens, carries orders and session bytes in order, closes |
| **4. Kaspar side** | FpgaILinkHandler on a CpuEnd link, sharing what it can with ILinkHandler | a session against a simulated gateway: login, order, ack, fill, keepalive |
| **5. On the card** | MAC, route from NY4 to the gateway, CME's test environment | orders in test, on real hardware |
| **6. Certification** | CME's order-entry certification | approved |

Phases 0 to 4 need no card.
