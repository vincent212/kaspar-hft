#!/usr/bin/env python3
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# kaspar_client.py -- place orders, read the BBO and fills from a running
# bnsim (or kaspr) over zmq. Needs pyzmq.
#
# The path:  zmq REQ -> MQ0 server (--mqport) -> CONS -> MTD -> SOM -> OB
#            (and CONS -> OB_<SYMBOL> for the book's own BBO)
#
# A request is colon-separated tokens: the actor, the command, then key/value
# pairs, e.g. "MTD:order:sym:BTCUSDT:sz:1:bs:BUY:px:6421694:x:BINANCE". The
# reply is text: either a line ("got order id: 3") or a table, a header line
# "Title<TAB>col<TAB>col..." followed by one "<TAB>value<TAB>value..." per row.
#
# Units are OB's: px in ticks (price / tick, binance.ini), sz in chunks.
#
# Demo:  kaspar_client.py [tcp://localhost:7777] [SYMBOL] [VENUE]
#        prints the BBO, rests one chunk a tick below the bid, buys one chunk
#        through the ask (fills on arrival), cancels the resting order, and
#        prints the orders and fills.
#
import sys
import time

import zmq


class Kaspar:
    """Orders, BBO and fills over kaspar's MQ0 server."""

    def __init__(self, addr="tcp://localhost:7777", timeout_ms=10000):
        self.ctx = zmq.Context.instance()
        self.addr = addr
        self.timeout_ms = timeout_ms
        self._connect()

    def _connect(self):
        self.sock = self.ctx.socket(zmq.REQ)
        self.sock.setsockopt(zmq.RCVTIMEO, self.timeout_ms)
        self.sock.setsockopt(zmq.SNDTIMEO, self.timeout_ms)
        self.sock.setsockopt(zmq.LINGER, 0)
        self.sock.connect(self.addr)

    def cmd(self, *tokens):
        """One request, one reply. A REQ socket cannot be reused after a
        timeout, so a timeout replaces it before raising."""
        try:
            self.sock.send_string(":".join(str(t) for t in tokens))
            return self.sock.recv_string()
        except zmq.Again:
            self.sock.close()
            self._connect()
            raise

    @staticmethod
    def table(text):
        """A reply table as a list of dicts, one per row."""
        lines = [l for l in text.splitlines() if l.strip()]
        if not lines:
            return []
        cols = [c for c in lines[0].split("\t")[1:] if c]
        return [dict(zip(cols, l.split("\t")[1:])) for l in lines[1:]]

    def bbo(self, sym):
        """(bid, ask) in ticks, from the book itself at the time of the
        request. MTD's "prices" holds only the last BBO OB pushed to it, and OB
        rate-limits those pushes, so it can be stale."""
        rep = self.cmd(f"OB_{sym}", "bbbo")          # "BBBO: <bid> <ask>"
        _, bid, ask = rep.split()
        return int(bid), int(ask)

    def order(self, sym, side, sz, px, venue):
        """A limit order; side BUY or SEL. Returns the order id."""
        rep = self.cmd("MTD", "order", "sym", sym, "sz", sz, "bs", side, "px", px, "x", venue)
        if not rep.startswith("got order id:"):
            raise RuntimeError(rep.strip())
        return int(rep.split(":")[1])

    def cancel(self, oid, venue):
        return self.cmd("MTD", "cancel", "id", oid, "x", venue).strip()

    def fills(self):
        """Every fill since the process started."""
        return self.table(self.cmd("MTD", "fills"))

    def orders(self):
        """Every order placed through MTD, with Y/N for fills, cancel sent,
        cancel acked, rejected."""
        return self.table(self.cmd("MTD", "get_orders"))


def demo(addr, sym, venue):
    k = Kaspar(addr)
    print("ping:", k.cmd("MTD", "ping").strip())
    for _ in range(30):                       # wait for the feed to publish a BBO
        bid, ask = k.bbo(sym)
        if bid > 0 and ask > 0:
            break
        time.sleep(1)
    print(f"{sym} bid {bid} ask {ask} (ticks)")

    rest = k.order(sym, "BUY", 1, bid - 1, venue)
    print(f"resting buy 1 @ {bid - 1}: order {rest}")
    # 100 ticks through the ask: the BBO above can be a moment old. OB fills a
    # marketable order of ours at its best ask, not at our limit.
    take = k.order(sym, "BUY", 1, ask + 100, venue)
    print(f"buy 1 @ {ask + 100} (through the ask): order {take}")

    time.sleep(3)
    print("cancel", rest, "->", k.cancel(rest, venue))
    time.sleep(2)

    print("orders:")
    for o in k.orders():
        print("  ", o)
    print("fills:")
    for f in k.fills():
        print("  ", f)


if __name__ == "__main__":
    demo(sys.argv[1] if len(sys.argv) > 1 else "tcp://localhost:7777",
         sys.argv[2] if len(sys.argv) > 2 else "BTCUSDT",
         sys.argv[3] if len(sys.argv) > 3 else "BINANCE")
