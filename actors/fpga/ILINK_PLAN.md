# Plan: iLink on the Alveo X3522PV, with FPGA actors

A separate project from the MDP3 handler ([MDP3_PLAN.md](MDP3_PLAN.md)), with its own
article. Not designed yet.

The aim: orders leave from the FPGA, as actors. iLink 3
is SBE messages over a FIXP session on TCP, so the card needs a TCP path, the
session, and pre-trade risk checks, and CME certifies order entry. Open: which
parts stay on the CPU (likely logging in, HMAC signing, retransmission) and which
run on the card (likely building and sending an order on an established session);
which port and route reach CME's gateway from NY4. Not started, and it needs its
own plan.

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
