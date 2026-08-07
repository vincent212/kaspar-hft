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
        {
            let mut g = self.inner.lock().unwrap();
            if !g.overflow.is_empty() || g.ring.len() >= self.cap {
                g.overflow.push_back(x);
            } else {
                g.ring.push_back(x);
            }
        }
        self.cv.notify_one();
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
}
