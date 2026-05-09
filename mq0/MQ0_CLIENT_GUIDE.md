<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# MQ0 Client Connection Guide

## Server Timeout Settings

The MQ0 server enforces the following timeouts to prevent resource exhaustion:

| Setting | Value | Description |
|---------|-------|-------------|
| Receive Timeout | 10 seconds | Server waits max 10s for client request |
| Send Timeout | 10 seconds | Server waits max 10s to send reply |
| TCP Keepalive | Enabled | Detects dead connections |
| Keepalive Idle | 30 seconds | Time before first probe |
| Keepalive Interval | 5 seconds | Time between probes |
| Keepalive Count | 3 | Probes before dropping connection |

## Client Requirements

### 1. Set Timeouts on Client Sockets

```python
import zmq

context = zmq.Context()
socket = context.socket(zmq.REQ)

# Must set timeouts - server will drop unresponsive clients
socket.setsockopt(zmq.RCVTIMEO, 10000)  # 10 seconds
socket.setsockopt(zmq.SNDTIMEO, 10000)  # 10 seconds
socket.setsockopt(zmq.LINGER, 0)        # Don't block on close

socket.connect("tcp://server:port")
```

### 2. Always Close Sockets

```python
# Option 1: Context manager
with context.socket(zmq.REQ) as socket:
    socket.setsockopt(zmq.RCVTIMEO, 10000)
    socket.setsockopt(zmq.LINGER, 0)
    socket.connect(addr)
    socket.send_string("command")
    response = socket.recv_string()
# socket auto-closed

# Option 2: try/finally
socket = context.socket(zmq.REQ)
try:
    socket.setsockopt(zmq.RCVTIMEO, 10000)
    socket.setsockopt(zmq.LINGER, 0)
    socket.connect(addr)
    socket.send_string("command")
    response = socket.recv_string()
finally:
    socket.close()
```

### 3. Reuse Sockets - Don't Create in Loops

```python
# BAD - creates new socket each iteration, leaks file descriptors
while True:
    socket = context.socket(zmq.REQ)
    socket.connect(addr)
    socket.send_string(msg)
    socket.recv_string()
    # socket never closed!

# GOOD - reuse socket
socket = context.socket(zmq.REQ)
socket.setsockopt(zmq.RCVTIMEO, 10000)
socket.setsockopt(zmq.LINGER, 0)
socket.connect(addr)
try:
    while True:
        socket.send_string(msg)
        response = socket.recv_string()
finally:
    socket.close()
```

### 4. Handle Timeouts Gracefully

```python
try:
    socket.send_string("command")
    response = socket.recv_string()
except zmq.Again:
    # Timeout occurred - server didn't respond in time
    print("Request timed out")
    # Consider: retry, reconnect, or fail gracefully
```

### 5. Reuse Context - One Per Process

```python
# BAD - multiple contexts waste resources
def send_request():
    ctx = zmq.Context()  # new context each call!
    sock = ctx.socket(zmq.REQ)
    ...

# GOOD - singleton context
class MQ0Client:
    def __init__(self):
        self.context = zmq.Context()

    def send_request(self, addr, msg):
        with self.context.socket(zmq.REQ) as socket:
            socket.setsockopt(zmq.RCVTIMEO, 10000)
            socket.setsockopt(zmq.LINGER, 0)
            socket.connect(addr)
            socket.send_string(msg)
            return socket.recv_string()
```

## Troubleshooting

### "Too many open files" Error

This means sockets aren't being closed. Check:
1. Are you creating sockets in a loop without closing them?
2. Are you using `socket.close()` or context managers?
3. Is `LINGER` set to 0?

Monitor open file descriptors:
```bash
lsof -p $(pgrep -f your_script.py) | wc -l
```

### Connection Dropped Unexpectedly

The server will drop connections that are idle for ~45 seconds (30s + 3x5s probes). If you need long-running connections, send periodic heartbeats or reconnect as needed.

## Reconnection Strategy

When disconnected, clients must recreate the socket. ZeroMQ REQ sockets cannot recover from a broken connection.

### Basic Reconnection Pattern

```python
import zmq
import time

class MQ0Client:
    def __init__(self, addr):
        self.addr = addr
        self.context = zmq.Context()
        self.socket = None
        self._connect()

    def _connect(self):
        """Create and configure a new socket."""
        if self.socket:
            self.socket.close()

        self.socket = self.context.socket(zmq.REQ)
        self.socket.setsockopt(zmq.RCVTIMEO, 10000)  # 10s timeout
        self.socket.setsockopt(zmq.SNDTIMEO, 10000)
        self.socket.setsockopt(zmq.LINGER, 0)
        self.socket.connect(self.addr)

    def send(self, msg, max_retries=3):
        """Send message with automatic reconnection on failure."""
        for attempt in range(max_retries):
            try:
                self.socket.send_string(msg)
                return self.socket.recv_string()
            except zmq.Again:
                # Timeout - recreate socket and retry
                print(f"Timeout, reconnecting (attempt {attempt + 1}/{max_retries})")
                self._connect()
            except zmq.ZMQError as e:
                # Connection error - recreate socket and retry
                print(f"ZMQ error: {e}, reconnecting (attempt {attempt + 1}/{max_retries})")
                self._connect()

        raise ConnectionError(f"Failed after {max_retries} attempts")

    def close(self):
        if self.socket:
            self.socket.close()
        self.context.term()
```

### Why Recreate the Socket?

ZeroMQ REQ/REP sockets have strict send-recv-send-recv ordering. If a timeout occurs after `send()` but before `recv()`, the socket is in a broken state and cannot be reused:

```python
# BAD - socket stuck after timeout
socket.send_string("cmd")
try:
    response = socket.recv_string()  # times out
except zmq.Again:
    socket.send_string("cmd")  # ERROR: socket expects recv, not send!

# GOOD - recreate socket after timeout
socket.send_string("cmd")
try:
    response = socket.recv_string()
except zmq.Again:
    socket.close()
    socket = context.socket(zmq.REQ)  # fresh socket
    socket.connect(addr)
    socket.send_string("cmd")  # works!
```

### Reconnection with Exponential Backoff

For production use, add backoff to avoid hammering the server:

```python
def send_with_backoff(self, msg, max_retries=5):
    """Send with exponential backoff on failures."""
    backoff = 0.1  # start at 100ms

    for attempt in range(max_retries):
        try:
            self.socket.send_string(msg)
            return self.socket.recv_string()
        except (zmq.Again, zmq.ZMQError) as e:
            if attempt == max_retries - 1:
                raise ConnectionError(f"Failed after {max_retries} attempts: {e}")

            print(f"Error: {e}, retrying in {backoff:.1f}s")
            time.sleep(backoff)
            backoff = min(backoff * 2, 5.0)  # cap at 5 seconds
            self._connect()
```

### Complete Example

```python
import zmq
import time

def main():
    client = MQ0Client("tcp://localhost:5555")

    try:
        while True:
            try:
                response = client.send("time")
                print(f"Server time: {response}")
            except ConnectionError as e:
                print(f"Connection lost: {e}")
                time.sleep(5)  # wait before retry loop continues

            time.sleep(1)
    finally:
        client.close()

if __name__ == "__main__":
    main()
```
