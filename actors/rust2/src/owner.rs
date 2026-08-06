// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! `MsgBox` — an owning smart pointer for a `dyn Message` that knows how to
//! reclaim its backing storage: either the global heap (like `Box`) or a
//! per-type object pool ([`crate::pool`]).
//!
//! This is the Rust analog of C++'s per-type `operator new`/`operator delete`:
//! the deallocation path is chosen by the concrete message type at allocation
//! time and carried alongside the type-erased pointer, so a `dyn Message` can be
//! freed back to the right pool even after its type is erased.

use std::alloc::Layout;
use std::ops::Deref;
use std::ptr::NonNull;

use crate::message::Message;
use crate::pool::PooledMessage;

/// How to reclaim a `MsgBox`'s storage after the value is dropped.
#[derive(Clone, Copy)]
enum Reclaim {
    /// Global allocator (message was heap-allocated like a `Box`).
    Heap,
    /// Return the raw block to a per-type pool via this function.
    Pool(unsafe fn(*mut u8)),
}

/// Owning handle to a `dyn Message`. Frees to the heap or a pool on drop.
pub struct MsgBox {
    ptr: NonNull<dyn Message>,
    reclaim: Reclaim,
}

// Safe: `Message: Send`, and `MsgBox` has unique ownership of the allocation.
unsafe impl Send for MsgBox {}

impl MsgBox {
    /// Heap-allocate a message (equivalent to `Box::new` + type erasure).
    #[inline]
    pub fn heap<T: Message>(value: T) -> MsgBox {
        let raw: *mut T = Box::into_raw(Box::new(value));
        let dynp: *mut dyn Message = raw;
        MsgBox {
            ptr: unsafe { NonNull::new_unchecked(dynp) },
            reclaim: Reclaim::Heap,
        }
    }

    /// Allocate a message from its per-type pool.
    #[inline]
    pub fn pooled<T: PooledMessage>(value: T) -> MsgBox {
        let block = T::__alloc();
        let tp = block as *mut T;
        unsafe { std::ptr::write(tp, value) };
        let dynp: *mut dyn Message = tp;
        MsgBox {
            ptr: unsafe { NonNull::new_unchecked(dynp) },
            reclaim: Reclaim::Pool(T::__free),
        }
    }

    /// Borrow the message.
    #[inline]
    pub fn as_message(&self) -> &dyn Message {
        unsafe { self.ptr.as_ref() }
    }
}

impl Deref for MsgBox {
    type Target = dyn Message;
    #[inline]
    fn deref(&self) -> &dyn Message {
        unsafe { self.ptr.as_ref() }
    }
}

impl Drop for MsgBox {
    fn drop(&mut self) {
        unsafe {
            let p = self.ptr.as_ptr();
            let thin = p.cast::<u8>();
            match self.reclaim {
                Reclaim::Heap => {
                    // Compute the layout BEFORE dropping (needs a live reference).
                    let layout = Layout::for_value(&*p);
                    std::ptr::drop_in_place(p);
                    // Zero-sized messages (e.g. unit-struct signals like Start/
                    // Shutdown) were never allocated by `Box`; calling dealloc
                    // with a zero-size layout is UB, so skip it.
                    if layout.size() != 0 {
                        std::alloc::dealloc(thin, layout);
                    }
                }
                Reclaim::Pool(free) => {
                    // Pool path needs no layout — the block goes back to the pool.
                    std::ptr::drop_in_place(p);
                    free(thin);
                }
            }
        }
    }
}

impl<T: Message> From<Box<T>> for MsgBox {
    #[inline]
    fn from(b: Box<T>) -> MsgBox {
        let dynp: *mut dyn Message = Box::into_raw(b);
        MsgBox {
            ptr: unsafe { NonNull::new_unchecked(dynp) },
            reclaim: Reclaim::Heap,
        }
    }
}

impl From<Box<dyn Message>> for MsgBox {
    #[inline]
    fn from(b: Box<dyn Message>) -> MsgBox {
        let dynp: *mut dyn Message = Box::into_raw(b);
        MsgBox {
            ptr: unsafe { NonNull::new_unchecked(dynp) },
            reclaim: Reclaim::Heap,
        }
    }
}
