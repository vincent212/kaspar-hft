// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Framework messages. Ids `< 16` are reserved for these.

use crate::define_message;

/// Sent to each actor once when the Manager starts it.
pub struct Start;
define_message!(Start, 4);

/// Sent to an actor to stop its run loop (matches C++ Shutdown id 5).
pub struct Shutdown;
define_message!(Shutdown, 5);
