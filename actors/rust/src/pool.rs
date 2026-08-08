// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Fixed-size object pool — a Rust port of the C++ `MemoryPool.hpp`.
//!
//! Per message type: a thread-local free-list (no lock on the fast path) backed by a
//! shared free-list under a `Mutex`, refilled in batches from cache-line-aligned
//! slabs. Mirrors the C++ three tiers (thread-local → shared → slab).
//!
//! Opt-in per type via [`define_pooled_message!`], with a tunable per-thread
//! cache size — the analog of C++'s `MemoryPool<T, AllocCache, ...>` base class
//! used by hot-path messages like `Data`, `BBBOChg`, `Fill`.
//!
//! Slabs are never returned to the OS (leaked at process exit) — matching the
//! C++ default, where `MemoryPool::cleanup()` is never called. This is fine for a
//! long-running trading process: the pool converges on the working-set size and
//! recycles blocks forever.

use std::alloc::Layout;
use std::sync::Mutex;

use crate::message::Message;

/// Cache-line size (matches C++ `CACHE_LINE_SIZE`).
pub const CACHE_LINE: usize = 64;

/// A raw block pointer made `Send` so it can live in the shared free-list.
///
/// Safe because the pool owns the blocks and hands ownership across threads
/// exactly like C++'s cross-thread free (producer allocates, consumer frees).
struct BlockPtr(*mut u8);
unsafe impl Send for BlockPtr {}

#[inline]
fn align_up(n: usize, align: usize) -> usize {
    (n + align - 1) & !(align - 1)
}

/// Shared (cross-thread) free-list + slab allocator for one message type.
/// Internal pool machinery. `pub` only so the exported `define_pooled_message!`
/// macro can name it from downstream crates — NOT a public API. Driving these
/// methods by hand (feeding foreign pointers, mismatched pools) is unsound.
#[doc(hidden)]
pub struct Shared {
    free: Mutex<Vec<BlockPtr>>,
    block_size: usize,
    align: usize,
}

impl Shared {
    /// Create a pool sized/aligned for `layout` (padded to a cache line).
    pub fn new(layout: Layout) -> Self {
        let align = layout.align().max(CACHE_LINE);
        let block_size = align_up(layout.size().max(1), align);
        Shared {
            free: Mutex::new(Vec::new()),
            block_size,
            align,
        }
    }

    /// Refill `local` up to `cap` blocks: first from the shared free-list (one
    /// lock, LIFO for cache hotness), then, if still empty, carve a fresh slab.
    pub fn refill(&self, local: &mut Vec<*mut u8>, cap: usize) {
        assert!(cap > 0, "pool cache size (cap) must be > 0");
        {
            let mut free = self.free.lock().unwrap();
            while local.len() < cap {
                match free.pop() {
                    Some(b) => local.push(b.0),
                    None => break,
                }
            }
        }
        if local.is_empty() {
            // One slab allocation split into `cap` cache-line-aligned blocks.
            // `checked_mul` guards against a size overflow that would otherwise
            // hand out `cap` pointers past a too-small allocation.
            let bytes = self
                .block_size
                .checked_mul(cap)
                .expect("pool slab size overflow");
            let slab = Layout::from_size_align(bytes, self.align)
                .expect("pool slab layout overflow");
            let base = unsafe { std::alloc::alloc(slab) };
            assert!(!base.is_null(), "pool slab allocation failed");
            for i in 0..cap {
                local.push(unsafe { base.add(i * self.block_size) });
            }
        }
    }

    /// Push blocks above `cap` from `local` back to the shared free-list (batched
    /// free — one lock for many blocks), matching C++'s `flushFreedCache`.
    pub fn flush(&self, local: &mut Vec<*mut u8>, cap: usize) {
        let mut free = self.free.lock().unwrap();
        while local.len() > cap {
            free.push(BlockPtr(local.pop().unwrap()));
        }
    }

    /// Number of blocks currently parked in the shared free-list (for tests).
    pub fn shared_len(&self) -> usize {
        self.free.lock().unwrap().len()
    }
}

/// A message type that allocates from a per-type pool. Implemented by
/// [`define_pooled_message!`]; do not implement or call by hand. The `__`
/// methods are macro-support surface, not public API.
///
/// # Safety
/// `__free` must only be called with a block previously returned by `__alloc`
/// for the *same* type, after its value has been dropped.
#[doc(hidden)]
pub unsafe trait PooledMessage: Message + Sized {
    /// Take a raw, uninitialized, correctly-sized/aligned block from the pool.
    fn __alloc() -> *mut u8;
    /// Return a block to the pool.
    ///
    /// # Safety
    /// See trait docs.
    unsafe fn __free(p: *mut u8);
}

/// Implement [`Message`] + [`PooledMessage`] for a type, giving it a per-type
/// object pool with a per-thread cache of `cap` blocks.
///
/// ```ignore
/// struct Data { /* ... */ }
/// define_pooled_message!(Data, 17, 16); // id 17, 16-block thread-local cache
/// ```
#[macro_export]
macro_rules! define_pooled_message {
    ($name:ty, $id:expr, $cap:expr) => {
        $crate::define_message!($name, $id);

        // A zero cache size would make the pool allocate a zero-byte slab (UB).
        const _: () = assert!($cap > 0, "pool cache size must be > 0");

        impl $name {
            #[inline]
            fn __with_local<R>(f: impl FnOnce(&mut ::std::vec::Vec<*mut u8>) -> R) -> R {
                thread_local! {
                    static LOCAL: ::std::cell::RefCell<::std::vec::Vec<*mut u8>> =
                        ::std::cell::RefCell::new(::std::vec::Vec::new());
                }
                LOCAL.with(|c| f(&mut c.borrow_mut()))
            }

            #[inline]
            fn __shared() -> &'static $crate::pool::Shared {
                static S: ::std::sync::OnceLock<$crate::pool::Shared> =
                    ::std::sync::OnceLock::new();
                S.get_or_init(|| {
                    $crate::pool::Shared::new(::std::alloc::Layout::new::<$name>())
                })
            }
        }

        unsafe impl $crate::pool::PooledMessage for $name {
            #[inline]
            fn __alloc() -> *mut u8 {
                <$name>::__with_local(|local| {
                    if let Some(p) = local.pop() {
                        return p;
                    }
                    <$name>::__shared().refill(local, $cap);
                    local.pop().expect("pool refill produced no block")
                })
            }

            #[inline]
            unsafe fn __free(p: *mut u8) {
                <$name>::__with_local(|local| {
                    local.push(p);
                    if local.len() > 2 * $cap {
                        <$name>::__shared().flush(local, $cap);
                    }
                })
            }
        }
    };
}
