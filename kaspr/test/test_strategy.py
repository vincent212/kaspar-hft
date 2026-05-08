#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

"""
test_strategy.py - Interactive test program for Kaspr trading system

Connects to kaspr via ZMQ and allows sending buy/sell orders interactively.

Usage:
    python test_strategy.py [--port 9001]

Commands:
    buy <quantity>   - Send buy order for ES
    sell <quantity>  - Send sell order for ES
    pos              - Query current position
    status           - Query system status
    msg              - Query message counts
    qlen             - Query queue lengths
    quit             - Exit the program

Example:
    $ python test_strategy.py
    Connected to kaspr at tcp://localhost:9001
    > buy 10
    Sent BUY 10 ES
    > sell 5
    Sent SELL 5 ES
    > pos
    Position: ES = 5
    > quit
"""

import zmq
import argparse
import sys


class KasprClient:
    """ZMQ client for Kaspr trading system."""

    def __init__(self, host: str = "localhost", port: int = 9001):
        self.endpoint = f"tcp://{host}:{port}"
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REQ)
        self.socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5s timeout
        self.socket.setsockopt(zmq.SNDTIMEO, 5000)
        self.socket.connect(self.endpoint)
        print(f"Connected to kaspr at {self.endpoint}")

    def send_command(self, cmd: str) -> str:
        """Send a command to kaspr and return the response."""
        try:
            self.socket.send_string(cmd)
            response = self.socket.recv_string()
            return response
        except zmq.ZMQError as e:
            return f"ERROR: {e}"

    def buy(self, quantity: int) -> str:
        """Send buy order for ES futures."""
        # Format: hedger buy <sym> <qty>
        cmd = f"hedger buy ES {quantity}"
        return self.send_command(cmd)

    def sell(self, quantity: int) -> str:
        """Send sell order for ES futures."""
        cmd = f"hedger sell ES {quantity}"
        return self.send_command(cmd)

    def position(self) -> str:
        """Query current position."""
        return self.send_command("rm pos")

    def status(self) -> str:
        """Query system status."""
        return self.send_command("status")

    def msg_counts(self) -> str:
        """Query message counts."""
        return self.send_command("msg")

    def queue_lengths(self) -> str:
        """Query queue lengths."""
        return self.send_command("qlen")

    def close(self):
        """Close the connection."""
        self.socket.close()
        self.context.term()


def parse_command(line: str) -> tuple:
    """Parse a user command into (command, args)."""
    parts = line.strip().split()
    if not parts:
        return None, []
    return parts[0].lower(), parts[1:]


def main():
    parser = argparse.ArgumentParser(description="Kaspr test strategy client")
    parser.add_argument("--host", default="localhost", help="Kaspr host")
    parser.add_argument("--port", type=int, default=9001, help="Kaspr MQ0 port")
    args = parser.parse_args()

    try:
        client = KasprClient(args.host, args.port)
    except Exception as e:
        print(f"Failed to connect: {e}")
        sys.exit(1)

    print("\nKaspr Test Strategy")
    print("Commands: buy <qty>, sell <qty>, pos, status, msg, qlen, quit")
    print("-" * 50)

    while True:
        try:
            line = input("> ").strip()
            if not line:
                continue

            cmd, cmd_args = parse_command(line)

            if cmd == "quit" or cmd == "exit":
                print("Goodbye!")
                break

            elif cmd == "buy":
                if not cmd_args:
                    print("Usage: buy <quantity>")
                    continue
                try:
                    qty = int(cmd_args[0])
                    print(f"Sending BUY {qty} ES...")
                    response = client.buy(qty)
                    print(f"Response: {response}")
                except ValueError:
                    print("Invalid quantity")

            elif cmd == "sell":
                if not cmd_args:
                    print("Usage: sell <quantity>")
                    continue
                try:
                    qty = int(cmd_args[0])
                    print(f"Sending SELL {qty} ES...")
                    response = client.sell(qty)
                    print(f"Response: {response}")
                except ValueError:
                    print("Invalid quantity")

            elif cmd == "pos":
                response = client.position()
                print(response)

            elif cmd == "status":
                response = client.status()
                print(response)

            elif cmd == "msg":
                response = client.msg_counts()
                print(response)

            elif cmd == "qlen":
                response = client.queue_lengths()
                print(response)

            elif cmd == "help":
                print("Commands:")
                print("  buy <qty>  - Send buy order for ES")
                print("  sell <qty> - Send sell order for ES")
                print("  pos        - Query current position")
                print("  status     - Query system status")
                print("  msg        - Query message counts")
                print("  qlen       - Query queue lengths")
                print("  quit       - Exit")

            else:
                # Send raw command to kaspr
                response = client.send_command(line)
                print(response)

        except KeyboardInterrupt:
            print("\nInterrupted. Type 'quit' to exit.")
        except EOFError:
            print("\nGoodbye!")
            break

    client.close()


if __name__ == "__main__":
    main()
