// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! `BQueue` — a faithful Rust port of the C++ blocking mailbox (`actors/cpp/include/actors/BQueue.hpp`).
//!
//! Preallocated fixed-capacity ring (fast path, zero realloc while under
//! capacity) + an unbounded `VecDeque` overflow, guarded by one `Mutex` +
//! `Condvar`. Overflow-sticky FIFO: once anything spills to overflow, all
//! pushes go to overflow until both drain (preserves ordering). `pop()` returns
//! `(value, last)` where `last` = "the queue is now empty" — the batching hint
//! the C++ `pop()` provides.
//!
//! Blocking, NOT lock-free — a deliberate match of the C++ design (low idle CPU;
//! the C++ roadmap flags replacing it with a lock-free ring as future work).

use std::collections::VecDeque;
use std::sync::{Condvar, Mutex};

struct Inner<T> {
    /// Fast path. Kept at len <= cap so it never reallocates.
    ring: VecDeque<T>,
    /// Overflow. Only used when the ring is full (or overflow already non-empty).
    overflow: VecDeque<T>,
}

pub struct BQueue<T> {
    inner: Mutex<Inner<T>>,
    cv: Condvar,
    cap: usize,
}

impl<T> BQueue<T> {
    /// Create a queue whose ring holds `cap` items before spilling to overflow.
    pub fn new(cap: usize) -> Self {
        BQueue {
            inner: Mutex::new(Inner {
                ring: VecDeque::with_capacity(cap),
                overflow: VecDeque::new(),
            }),
            cv: Condvar::new(),
            cap,
        }
    }

    /// Push a value (never blocks, never drops). Overflow-sticky, like C++.
    pub fn push(&self, x: T) {
        let was_empty = {
            let mut g = self.inner.lock().unwrap();
            let empty = g.ring.is_empty() && g.overflow.is_empty();
            if !g.overflow.is_empty() || g.ring.len() >= self.cap {
                g.overflow.push_back(x);
            } else {
                g.ring.push_back(x);
            }
            empty
        };
        // The consumer only sleeps (cv.wait) when it finds the queue empty under
        // the lock, so the only push that must signal is the one that makes an
        // empty queue non-empty. Skips a syscall on every push into a backlog.
        if was_empty {
            self.cv.notify_one();
        }
    }

    /// Batch-blocking drain. Blocks until >=1 item, then moves the ENTIRE queue
    /// (ring then overflow, FIFO) into `out` under a single lock. N queued
    /// messages cost one lock acquisition instead of N. Returns nothing; `out`
    /// is cleared first.
    pub fn pop_batch(&self, out: &mut Vec<T>) {
        out.clear();
        let mut g = self.inner.lock().unwrap();
        loop {
            if !g.ring.is_empty() || !g.overflow.is_empty() {
                out.extend(g.ring.drain(..));
                out.extend(g.overflow.drain(..));
                return;
            }
            g = self.cv.wait(g).unwrap();
        }
    }

    /// Blocking pop. Returns `(value, last)` where `last` means the queue is now
    /// empty. Drains the ring first, then overflow (FIFO).
    pub fn pop(&self) -> (T, bool) {
        let mut g = self.inner.lock().unwrap();
        loop {
            if let Some(x) = g.ring.pop_front() {
                let last = g.ring.is_empty() && g.overflow.is_empty();
                return (x, last);
            }
            if let Some(x) = g.overflow.pop_front() {
                let last = g.ring.is_empty() && g.overflow.is_empty();
                return (x, last);
            }
            g = self.cv.wait(g).unwrap();
        }
    }

    /// Number of queued items.
    pub fn len(&self) -> usize {
        let g = self.inner.lock().unwrap();
        g.ring.len() + g.overflow.len()
    }

    pub fn is_empty(&self) -> bool {
        let g = self.inner.lock().unwrap();
        g.ring.is_empty() && g.overflow.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fifo_and_last_flag() {
        let q = BQueue::new(2);
        q.push(1);
        q.push(2);
        q.push(3); // spills to overflow
        assert_eq!(q.len(), 3);
        assert_eq!(q.pop(), (1, false));
        assert_eq!(q.pop(), (2, false));
        assert_eq!(q.pop(), (3, true)); // last
        assert!(q.is_empty());
    }

    #[test]
    fn overflow_sticky_preserves_order() {
        // cap 1: after one item the rest go to overflow; ordering must hold.
        let q = BQueue::new(1);
        for i in 0..10 {
            q.push(i);
        }
        for i in 0..10 {
            let (v, last) = q.pop();
            assert_eq!(v, i);
            assert_eq!(last, i == 9);
        }
    }

    // ---- batched-drain scenarios ----

    #[test]
    fn pop_batch_drains_ring_in_fifo() {
        let q = BQueue::new(8);
        for i in 0..5 {
            q.push(i);
        }
        let mut out = Vec::new();
        q.pop_batch(&mut out);
        assert_eq!(out, vec![0, 1, 2, 3, 4]);
        assert!(q.is_empty());
    }

    #[test]
    fn pop_batch_drains_ring_then_overflow_fifo() {
        // cap 2: 0,1 in ring; 2,3,4 spill to overflow. One drain must return
        // all five in order (ring first, then overflow).
        let q = BQueue::new(2);
        for i in 0..5 {
            q.push(i);
        }
        let mut out = Vec::new();
        q.pop_batch(&mut out);
        assert_eq!(out, vec![0, 1, 2, 3, 4]);
        assert!(q.is_empty());
    }

    #[test]
    fn pop_batch_clears_out_param() {
        let q = BQueue::new(4);
        q.push(42);
        let mut out = vec![7, 8, 9]; // stale contents must be discarded
        q.pop_batch(&mut out);
        assert_eq!(out, vec![42]);
    }

    #[test]
    fn pop_batch_returns_only_currently_queued() {
        let q = BQueue::new(8);
        q.push(1);
        q.push(2);
        let mut out = Vec::new();
        q.pop_batch(&mut out);
        assert_eq!(out, vec![1, 2]);
        q.push(3); // arrived after the drain
        q.pop_batch(&mut out);
        assert_eq!(out, vec![3]);
    }

    #[test]
    fn pop_batch_blocks_until_pushed_then_wakes() {
        // A consumer blocked in pop_batch on an empty queue must be woken by a
        // push into the empty queue (exercises the transition-notify path).
        use std::sync::Arc;
        use std::thread;
        use std::time::Duration;

        let q = Arc::new(BQueue::new(4));
        let qc = Arc::clone(&q);
        let h = thread::spawn(move || {
            let mut out = Vec::new();
            qc.pop_batch(&mut out); // blocks until producer pushes
            out
        });
        thread::sleep(Duration::from_millis(50)); // ensure consumer is parked
        q.push(99);
        let got = h.join().unwrap();
        assert_eq!(got, vec![99]);
    }

    #[test]
    fn concurrent_producer_consumer_in_order() {
        // Single producer streams 0..N; single consumer drains via pop_batch.
        // Every item must arrive exactly once, in order. Stresses the
        // transition-notify wakeup under a real backlog/drain race.
        use std::sync::Arc;
        use std::thread;

        const N: i32 = 200_000;
        let q = Arc::new(BQueue::new(1024));
        let qp = Arc::clone(&q);
        let producer = thread::spawn(move || {
            for i in 0..N {
                qp.push(i);
            }
        });
        let qc = Arc::clone(&q);
        let consumer = thread::spawn(move || {
            let mut expect = 0;
            let mut buf = Vec::new();
            while expect < N {
                qc.pop_batch(&mut buf);
                for &v in &buf {
                    assert_eq!(v, expect);
                    expect += 1;
                }
            }
            expect
        });
        producer.join().unwrap();
        assert_eq!(consumer.join().unwrap(), N);
    }
}
