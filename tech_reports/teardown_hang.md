# The "teardown hang" is a join on an untimed `select()`

Short version: kaspr/md_perf_meter does not hang *after* flushing. It flushes,
then `Manager::end()` joins the SocketReader threads, and those threads are
parked inside `select(..., NULL)` with no timeout and no wakeup path. The join
never returns. Under onload the parked threads busy-poll, so an abandoned
process **burns about one core per accelerated reader, indefinitely**.

The old label — "hangs in teardown after flushing, `futex_wait_queue`,
harmless" — got two things wrong. `futex_wait_queue` is the *main* thread's
wchan, which is just what a `pthread_join` looks like from `/proc`; it names the
waiter, not the cause. And it is not harmless: the process keeps its multicast
memberships and its bound ports, which is what later shows up as `EADDRINUSE`
on the next run.

---

## 1. The specimen

pid 1333987, `/tmp/kh_fixw/kaspr/src/md_perf_meter config/md_perf.ini`.
Started 13:18:29, SIGTERM'd shortly after. Its `lat_*` output files stopped
growing at 13:23. At the time of this diagnosis it had been alive 60 minutes.

So: the samples were on disk and complete for 30+ minutes while the process
refused to exit. That part of the old label was right.

```
    PID STAT     ELAPSED WCHAN                CMD
1333987 Sl      01:00:29 futex_wait_queue     .../md_perf_meter config/md_perf.ini
```

## 2. It is not idle

9 threads. Two of them in `R`, not `S`:

| tid | state | utime+stime (jiffies) |
|---|---|---|
| 1334096 | R | 256964 |
| 1334097 | R | 257013 |

~2570 CPU-seconds each. Two samples of `/proc/PID/stat` three wall-seconds
apart:

```
jiffies: 818286 -> 819015   (delta 729)
```

729 jiffies / 3 s = **2.43 cores busy**, while doing nothing, while "hung".
That is the number that kills the "harmless" claim. It also still held 46 open
fds.

## 3. The stacks

`gdb -p 1333987 -batch -ex 'thread apply all bt'`.

**Main thread — the waiter:**

```
__futex_abstimed_wait_common64
__pthread_clockjoin_ex
std::thread::join (this=0x3a7b840)
actors::Manager::end()
main at kaspr.cpp:907
```

Waiting on `expected=1334086`.

**Threads 4,5,6,7,8,9 — the six SocketReaders, all identical:**

```
chutil::mcast::wait_for_data      (chutil/udp_socket.hpp:125)
SocketReader<unsigned int,0>::continue_handler_new (SocketReader.hpp:258)
actors::Actor::call_handler
actors::Actor::process_message_internal
actors::Actor::operator()
```

Threads 4,5,8,9 are below that in the kernel, in `__GI___select` — blocked,
costing nothing. Threads 6,7 are in **onload's user-level select**:

```
citp_ul_select      (onload/.../poll_select.c:178)
ci_frc64            (onload/.../gcc_x86.h:56)
citp_ul_do_select   (..., timeout_ms=1844674407370, ...)
```

Those two are 1334096 and 1334097. onload intercepts `select()` and spins in
userspace rather than sleeping; with an effectively infinite `timeout_ms` it
spins forever. That is the 2.43 cores, and it is why the cost scales with how
many of your readers are on accelerated sockets, not with how many readers you
have.

Threads 2,3 are ZMQ background threads, idle, irrelevant.

## 4. The code

Three pieces, and all three have to be true at once.

**(a) One `select()`, no timeout.** `chutil/include/chutil/udp_socket.hpp:119`:

```cpp
inline static void wait_for_data(int sock)
{
   fd_set read_fds;
   FD_ZERO(&read_fds);
   FD_SET(sock, &read_fds);
   // Block until data is available on the socket
   int result = select(sock + 1, &read_fds, NULL, NULL, NULL);
   if (result < 0) { perror("select"); return; }
}
```

The 5th argument is the timeout. `NULL` means block until a datagram arrives.
Nothing else makes it return. This is the only untimed wait in the read path —
the tree's other `select`, `chutil/sock_help.hpp:67`, does pass a `&timeout` and
is on the connect path.

**(b) The loop only tests its exit flag at the top.**
`mcast_recv/include/mcast_recv/act/SocketReader.hpp:248`:

```cpp
while (read_loop)
{
  chutil::mcast::wait_for_data(sock);   // :258  <-- parked here
L1:
  ...
}
```

A thread inside `wait_for_data` is past the test and cannot re-reach it.

**(c) `end()` cannot help, and is not called anyway.** `SocketReader.hpp:289`:

```cpp
void end()
{
  read_loop = false;
  close(sock);
  log_inf("end: closed socket");
}
```

Both statements are useless against a thread already in `select()`:

- `read_loop = false` is only read at the top of the loop (see (b)).
- `close(sock)` on an fd another thread is already blocked in `select()` on
  does **not** wake that thread on Linux. The fd is unlinked from the table;
  the sleeping waiter is not signalled. (This is a well-known POSIX wart, not a
  local bug — but the code is written as if it worked.)

And `end()` is unreachable regardless: the actor thread is *inside*
`continue_handler_new`, so it never returns to `Actor::operator()` to dequeue
the `Shutdown` that `Manager::end()` just sent it.

**(d) The join is unconditional.** `actors/cpp/Manager.cpp:104`:

```cpp
void Manager::end()
{
  if (!terminate_called) terminate_called = true;
  for (auto actor : actor_list) actor->send(new msg::Shutdown());   // nobody reads it
  for (auto t : thread_list) { if (t->joinable()) t->join(); }      // forever
}
```

Note also `~Manager()` at `Manager.cpp:97`, whose comment says

```
// Join all threads with timeout - if they don't join in 2 seconds, detach them
```

There is no timeout and no detach. The body is a plain `p->join()`. The comment
describes a fix that was never written.

## 5. Why SIGTERM is not enough, and SIGKILL is

`shutdown_handler` runs, the recorders flush and close, `Manager::end()` is
entered, and the process stops there. The signal was delivered and handled
correctly; the flush completed correctly. There is simply no path from "a
SocketReader thread is in `select()`" to "that thread returns".

`SIGKILL` works because the kernel does not ask. The samples are already on
disk by then, which is why `SIGKILL` is safe here — see §8.

**This is the procedure, not a stopgap.** §7 says why nothing is being changed
in code.

```bash
kill -TERM <pid>
sleep 3
# confirm the sample files have stopped growing, then:
kill -KILL <pid> 2>/dev/null
```

Do not skip the TERM. It is what writes the `decode packets=` verdict line and
closes the bin recorder cleanly. Do not skip the KILL either, or the ports stay
held and the next run dies with `EADDRINUSE` — a failure mode that has already
been misread once as a permissions problem.

## 6. How to tell this apart from a real hang

If you see a kaspr process that will not die:

```bash
# 1. Is it spinning? A parked-select process on onload burns cores.
a=$(awk '{print $14+$15}' /proc/$P/stat); sleep 3
b=$(awk '{print $14+$15}' /proc/$P/stat); echo $((b-a))   # ~700 = ~2.4 cores

# 2. Where is main?
gdb -p $P -batch -ex 'thread apply all bt 12' | grep -A3 'Thread 1'
#    Manager::end -> std::thread::join  => this defect

# 3. Where are the readers?
gdb -p $P -batch -ex 'thread apply all bt 12' | grep -c wait_for_data
#    equals your SocketReader count => this defect
```

If main is NOT in `Manager::end()`, or the readers are not in
`wait_for_data`, it is something else and this document does not apply.

## 7. Decision: TERM then KILL. Not fixed in code.

**Settled. The procedure in §5 is the answer, not a workaround pending a fix.**

The reason is that every candidate fix lands in the per-packet path, and the
per-packet path is the thing this box exists to measure. Spelled out so it is
not reopened:

**Timeout on `select()`** — the tempting one-liner. `wait_for_data` has exactly
one caller in the tree, `SocketReader.hpp:258`, and that is the production
receive loop for every channel. `t0` (`m->recv_ts`) is stamped in `read()`
immediately after it returns, so this `select()` sits at the front of every
packet's leg-1. Also, onload branches on the timeout: the backtrace shows
`citp_ul_do_select(..., timeout_ms=1844674407370, ...)`, the "infinite" case. A
finite deadline puts a deadline check inside onload's spin loop and interacts
with `EF_POLL_USEC=3000` / `EF_INT_DRIVEN=0` in a way nobody has measured.

**Interrupting with a signal** — looks free, because `wait_for_data` already
returns on `result < 0`. It is not free, because **`wait_for_data` returns
`void`**, so the caller cannot tell EINTR from data. `continue_handler_new`
enters its inner loop with `just_got_data = true` unconditionally
(`SocketReader.hpp:260`), so after a spurious return it goes straight into
`read()` without re-testing `read_loop`. `read()` then calls `recvfrom` on an
empty **blocking** socket — `create_udp_socket` sets no `O_NONBLOCK` and no
`SO_RCVTIMEO` — and blocks there instead. That is worse: `recvfrom` has no
timeout argument, and `read()`'s `while (!nrec)` defeats a second signal anyway,
since `receive()` treats a non-EAGAIN error as `0` and the loop retries forever
while spewing `perror`. Making this work needs three coordinated hot-path edits:
a return code on `wait_for_data`, a caller that branches on it, and a bounded
retry in `read()`.

**Self-pipe / eventfd** — the textbook answer, and correct. Rejected for the
same reason: another fd in the `FD_SET` and a wider `nfds` on every call.

**Bounded join** (`pthread_timedjoin_np` + detach in `Manager::end()`) is the
only candidate that touches nothing measured. It is also not a fix — it does not
stop the spinning thread, it just lets the process reach `exit()`. Which is
exactly what `kill -9` already does, in one line, from outside the binary.

So the code keeps the defect and we keep killing it. What *is* worth doing is
the comment at `Manager.cpp:97`, which currently promises a 2-second timeout and
a detach that do not exist; it should say what the code actually does and point
here.

## 8. Effect on measurements

None on data already written. The flush completes before the join is reached,
so every `lat_*.msg`, `.csv` and `.arr` file is intact.

Two indirect effects to be aware of:

- **A forgotten process taxes the next run.** 2.43 cores of onload spin is
  contention on a box where `EF_POLL_USEC=3000` means the thing you are
  measuring is also spinning. Check `pgrep -ax kaspr md_perf_meter` before
  every window; this is already step 3 of the dual-path runbook.
- **Held ports look like a config or privilege fault.** They are not. See §5.
