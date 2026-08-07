// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Manager: one OS thread per actor, optional CPU affinity + real-time priority
//! (Linux only; no-op elsewhere). Mirrors the C++ `Manager`.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;

use crate::actor::{run_actor, Actor, ActorCell, ActorRef, Envelope, ACTOR_BQUEUE_SIZE};
use crate::messages::{Shutdown, Start};
use crate::owner::MsgBox;
use crate::queue::BQueue;

/// Per-actor thread placement. Defaults to "no pinning, normal scheduling".
#[derive(Clone, Default)]
pub struct ThreadConfig {
    /// CPU cores to pin to (Linux only). Empty = no affinity.
    pub affinity: Vec<usize>,
    /// SCHED_FIFO priority (Linux only). 0 = SCHED_OTHER / normal.
    pub priority: i32,
}

/// Handle used by actors to request orderly shutdown of the whole system.
#[derive(Clone)]
pub struct ManagerHandle {
    cells: Arc<Mutex<Vec<Arc<ActorCell>>>>,
    done: Arc<(Mutex<bool>, Condvar)>,
}

impl ManagerHandle {
    /// Look up a local actor by name, returning a `Local` `ActorRef` or `None`.
    ///
    /// `ManagerHandle` is `Clone` + `Send` + `Sync`, so this is the resolver to
    /// hand to `interop::register_local_lookup` for cross-language inbound
    /// delivery: `register_local_lookup(move |n| handle.get_ref_local(n))`.
    pub fn get_ref_local(&self, name: &str) -> Option<ActorRef> {
        self.cells
            .lock()
            .unwrap()
            .iter()
            .find(|c| c.name == name)
            .map(|c| ActorRef::local(c.clone()))
    }

    /// Broadcast Shutdown to every actor and wake `Manager::run`.
    pub fn terminate(&self) {
        for c in self.cells.lock().unwrap().iter() {
            c.queue.push(Envelope {
                msg: MsgBox::heap(Shutdown),
                sender: None,
            });
        }
        let (m, cv) = &*self.done;
        *m.lock().unwrap() = true;
        cv.notify_all();
    }
}

pub struct Manager {
    cells: Arc<Mutex<Vec<Arc<ActorCell>>>>,
    pending: Vec<(Arc<ActorCell>, ThreadConfig)>,
    threads: Vec<JoinHandle<()>>,
    done: Arc<(Mutex<bool>, Condvar)>,
    /// True once `init()` has run, so later `manage()` calls spawn immediately
    /// instead of silently queuing a never-started actor.
    started: bool,
}

impl Manager {
    pub fn new() -> Self {
        Manager {
            cells: Arc::new(Mutex::new(Vec::new())),
            pending: Vec::new(),
            threads: Vec::new(),
            done: Arc::new((Mutex::new(false), Condvar::new())),
            started: false,
        }
    }

    pub fn get_handle(&self) -> ManagerHandle {
        ManagerHandle {
            cells: self.cells.clone(),
            done: self.done.clone(),
        }
    }

    /// Register an actor. Returns its `ActorRef` immediately (usable for
    /// `fast_send` right away; async delivery starts after `init`).
    pub fn manage(&mut self, name: &str, actor: Box<dyn Actor>, cfg: ThreadConfig) -> ActorRef {
        let cell = Arc::new(ActorCell {
            name: name.to_string(),
            actor: Mutex::new(actor),
            queue: BQueue::new(ACTOR_BQUEUE_SIZE),
            running: AtomicBool::new(true),
        });
        self.cells.lock().unwrap().push(cell.clone());
        if self.started {
            // Registered after init(): spawn now, otherwise it would be a dead
            // actor whose async messages are silently dropped (only fast_send
            // would work).
            self.spawn_cell(cell.clone(), cfg);
        } else {
            self.pending.push((cell.clone(), cfg));
        }
        ActorRef::local(cell)
    }

    /// Look up an actor by name among the **local** actors this Manager owns.
    /// Returns a `Local` `ActorRef`, or `None`. Delegates to
    /// [`ManagerHandle::get_ref_local`] so the lookup lives in one place.
    pub fn get_ref_local(&self, name: &str) -> Option<ActorRef> {
        self.get_handle().get_ref_local(name)
    }

    /// Location-transparent lookup: a local actor if present, otherwise (with the
    /// `interop` feature) a C++ actor reachable over FFI. `sender` is the name to
    /// attach for replies from a cross-language target.
    pub fn get_ref(&self, name: &str, sender: &str) -> Option<ActorRef> {
        if let Some(r) = self.get_ref_local(name) {
            return Some(r);
        }
        #[cfg(feature = "interop")]
        {
            crate::interop::resolve_cpp(name, sender)
        }
        #[cfg(not(feature = "interop"))]
        {
            let _ = sender;
            None
        }
    }

    /// Spawn one actor's thread and deliver its `Start`.
    fn spawn_cell(&mut self, cell: Arc<ActorCell>, cfg: ThreadConfig) {
        let run_cell = cell.clone();
        let t = std::thread::spawn(move || {
            apply_thread_config(&cfg);
            run_actor(run_cell);
        });
        self.threads.push(t);
        cell.queue.push(Envelope {
            msg: MsgBox::heap(Start),
            sender: None,
        });
    }

    /// Spawn one thread per pending actor and send each a `Start`. After this,
    /// later `manage()` calls spawn immediately.
    pub fn init(&mut self) {
        self.started = true;
        let pending = std::mem::take(&mut self.pending);
        for (cell, cfg) in pending {
            self.spawn_cell(cell, cfg);
        }
    }

    /// Block until an actor calls `ManagerHandle::terminate`.
    pub fn run(&self) {
        let (m, cv) = &*self.done;
        let mut g = m.lock().unwrap();
        while !*g {
            g = cv.wait(g).unwrap();
        }
    }

    /// Shut every actor down and join all threads. Idempotent: safe to call
    /// again (e.g. from `Drop`) after an explicit call.
    pub fn end(&mut self) {
        // Poison-safe lock so `Drop` can never panic here.
        for c in self.cells.lock().unwrap_or_else(|e| e.into_inner()).iter() {
            if c.running.load(Ordering::Relaxed) {
                c.queue.push(Envelope {
                    msg: MsgBox::heap(Shutdown),
                    sender: None,
                });
            }
        }
        for t in self.threads.drain(..) {
            let _ = t.join();
        }
    }
}

impl Default for Manager {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Manager {
    fn drop(&mut self) {
        // Shut down and join actor threads even if `end()` was never called —
        // otherwise the threads are detached and leak, parked forever in pop().
        self.end();
    }
}

#[cfg(target_os = "linux")]
fn apply_thread_config(cfg: &ThreadConfig) {
    unsafe {
        if !cfg.affinity.is_empty() {
            let mut set: libc::cpu_set_t = std::mem::zeroed();
            libc::CPU_ZERO(&mut set);
            for &core in &cfg.affinity {
                libc::CPU_SET(core, &mut set);
            }
            libc::sched_setaffinity(0, std::mem::size_of::<libc::cpu_set_t>(), &set);
        }
        if cfg.priority > 0 {
            let param = libc::sched_param {
                sched_priority: cfg.priority,
            };
            libc::sched_setscheduler(0, libc::SCHED_FIFO, &param);
        }
    }
}

#[cfg(not(target_os = "linux"))]
fn apply_thread_config(_cfg: &ThreadConfig) {
    // Affinity / RT priority are Linux-only; no-op on other platforms.
}
