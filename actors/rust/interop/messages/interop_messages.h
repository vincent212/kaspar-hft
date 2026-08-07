/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * ---------------------------------------------------------------------------
 * INTEROP MESSAGE SCHEMA — the single source of truth for C++/Rust interop.
 * ---------------------------------------------------------------------------
 *
 * This is the ONLY file you edit to add a cross-language message. From it,
 * `codegen/generate.py` produces the matching Rust and C++ definitions, the
 * marshalling (`to_c`/`from_c`), and the dispatch tables for both sides — so the
 * two languages can never drift out of sync (see interop/README.md).
 *
 * C++ includes this header directly for the wire structs; Rust gets a generated
 * `#[repr(C)]` mirror. Nothing that owns heap memory crosses the FFI boundary.
 *
 * RULES (enforced by convention + the generator):
 *   - Fixed-width integers only: int32_t / int64_t / uint32_t / uint64_t
 *     (never `int`/`long` — their width is ABI-unstable).
 *   - double / float are allowed.
 *   - Booleans: declare as int32_t and put a `bool` comment on the SAME line
 *     (1 = true, 0 = false). The generator emits a real bool on each side.
 *   - Strings: use `interop_string` (fixed-size, no heap).
 *   - Arrays: `double prices[5];` -> `[f64; 5]` / `std::array<double,5>`.
 *   - Message ids: 400-499, and MUST be < 512 (also < actors HANDLER_CACHE_SIZE
 *     of 1024). Ids < 16 are reserved by actors for framework messages.
 */

#ifndef INTEROP_MESSAGES_H
#define INTEROP_MESSAGES_H

#include <stdint.h>

/* Annotation the generator greps for. Expands to nothing when compiled. */
#define INTEROP_MESSAGE(name, id)

/* Fixed-size string for FFI (no heap allocation). */
#define INTEROP_STRING_MAX 64

typedef struct {
    char     data[INTEROP_STRING_MAX];
    uint32_t len;
} interop_string;

/* ============================================================
 * Request / reply demo messages
 * ============================================================ */

INTEROP_MESSAGE(Ping, 400)
typedef struct {
    int32_t count;
} Ping;

INTEROP_MESSAGE(Pong, 401)
typedef struct {
    int32_t count;
} Pong;

INTEROP_MESSAGE(DataRequest, 402)
typedef struct {
    int32_t        request_id;
    interop_string symbol;
} DataRequest;

INTEROP_MESSAGE(DataResponse, 403)
typedef struct {
    int32_t request_id;
    double  value;
    int32_t found;   /* bool: 1 = found, 0 = not found */
} DataResponse;

#endif /* INTEROP_MESSAGES_H */
