#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.

"""
Code generator for actors C++/Rust FFI interop.

Reads ONE input — messages/interop_messages.h — and emits matching definitions,
marshalling, and dispatch for both languages, so the two sides can never drift.

Emitted (relative to actors/rust/):
  src/interop/generated.rs              Rust: wire + native structs, define_message!,
                                        to_c/from_c, inbound dispatch, outbound
                                        marshalling, cpp lookup, register()
  interop/generated/cpp/InteropMessages.hpp   C++ msg:: classes (to/from C struct)
  interop/generated/cpp/CppActorBridge.hpp/.cpp   extern "C" C++ bridge (called by Rust)
  interop/generated/cpp/RustActorIF.hpp       C++ helper to call Rust actors

The Rust side is compiled and tested here. The C++ side targets the public
actors/cpp API (actors::Message_N<N>, Actor::send/fast_send) and is built with the
hybrid examples (see interop/README.md); it is not compiled by `cargo`.

Usage:
    python3 generate.py            # uses the default paths (regenerate in place)
    python3 generate.py <header> <crate_root>
"""

import os
import re
import sys
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class Field:
    name: str
    c_type: str
    is_string: bool = False
    is_bool: bool = False
    array_size: Optional[int] = None


@dataclass
class Message:
    name: str
    msg_id: int
    fields: List[Field]


# ---------------------------------------------------------------------------
# Parse
# ---------------------------------------------------------------------------

def parse_header(path: str) -> List[Message]:
    with open(path) as f:
        content = f.read()

    messages: List[Message] = []
    pattern = (
        r'INTEROP_MESSAGE\s*\(\s*(\w+)\s*,\s*(\d+)\s*\)\s*'
        r'typedef\s+struct\s*\{([^}]*)\}\s*(\w+)\s*;'
    )
    for m in re.finditer(pattern, content, re.DOTALL):
        name, msg_id, body, struct_name = m.group(1), int(m.group(2)), m.group(3), m.group(4)
        assert name == struct_name, f"annotation/struct name mismatch: {name} vs {struct_name}"
        fields: List[Field] = []
        for fm in re.finditer(r'(\w+)\s+(\w+)(?:\[(\d+)\])?\s*;', body):
            c_type, fname = fm.group(1), fm.group(2)
            array_size = int(fm.group(3)) if fm.group(3) else None
            is_string = c_type == 'interop_string'
            # bool if a `bool` comment sits on the same source line
            line_end = body.find('\n', fm.end())
            line_end = len(body) if line_end == -1 else line_end
            is_bool = ('bool' in body[fm.end():line_end].lower()) and array_size is None
            fields.append(Field(fname, c_type, is_string, is_bool, array_size))
        messages.append(Message(name, msg_id, fields))

    _validate(content, messages)
    return messages


def _validate(content: str, messages: List[Message]) -> None:
    """Fail loudly rather than silently dropping or mis-generating a message."""
    # Every INTEROP_MESSAGE annotation must have produced a parsed message. A
    # mismatch means a struct body didn't match (e.g. a `}` inside a comment),
    # which would otherwise silently omit the message from BOTH languages.
    # Count annotations with a numeric id (the `#define INTEROP_MESSAGE(name, id)`
    # line has a non-numeric second arg, so it is correctly excluded).
    annotated = len(re.findall(r'INTEROP_MESSAGE\s*\(\s*\w+\s*,\s*\d+\s*\)', content))
    if annotated != len(messages):
        raise SystemExit(
            f'error: {annotated} INTEROP_MESSAGE annotation(s) but parsed {len(messages)} '
            f'struct(s). A message failed to parse (a `}}` inside a field comment, or a '
            f'malformed struct?). Fix the header — silent drops defeat the point of codegen.'
        )
    # Ids: unique, and in the range the framework/header require. `< 16` is
    # reserved by actors; `< 512` is the interop ceiling (also < HANDLER_CACHE_SIZE).
    seen = {}
    for m in messages:
        if not (16 <= m.msg_id < 512):
            raise SystemExit(
                f'error: {m.name} id {m.msg_id} out of range — must be 16..511 '
                f'(interop convention is 400..499).'
            )
        if m.msg_id in seen:
            raise SystemExit(
                f'error: duplicate message id {m.msg_id}: {seen[m.msg_id]} and {m.name}.'
            )
        seen[m.msg_id] = m.name


# ---------------------------------------------------------------------------
# Type maps
# ---------------------------------------------------------------------------

_RUST_WIRE = {  # C type -> Rust FFI (#[repr(C)]) type
    'int32_t': 'i32', 'int64_t': 'i64', 'uint32_t': 'u32', 'uint64_t': 'u64',
    'double': 'f64', 'float': 'f32', 'char': 'u8', 'interop_string': 'CInteropString',
}
_RUST_NATIVE = {  # C type -> ergonomic Rust type
    'int32_t': 'i32', 'int64_t': 'i64', 'uint32_t': 'u32', 'uint64_t': 'u64',
    'double': 'f64', 'float': 'f32', 'char': 'u8', 'interop_string': 'String',
}


def rust_wire(f: Field) -> str:
    t = _RUST_WIRE.get(f.c_type, f.c_type)
    return f'[{t}; {f.array_size}]' if f.array_size else t


def rust_native(f: Field) -> str:
    if f.is_bool:
        return 'bool'
    t = _RUST_NATIVE.get(f.c_type, f.c_type)
    return f'[{t}; {f.array_size}]' if f.array_size else t


# ---------------------------------------------------------------------------
# Rust emitter (compiled + tested here)
# ---------------------------------------------------------------------------

def generate_rust(messages: List[Message], out_path: str) -> None:
    o: List[str] = []
    w = o.append

    w('// AUTO-GENERATED by interop/codegen/generate.py — DO NOT EDIT.')
    w('// Regenerate: python3 interop/codegen/generate.py')
    w('//')
    w('// Source of truth: interop/messages/interop_messages.h')
    w('#![allow(dead_code)]')
    w('')
    w('use std::os::raw::{c_char, c_int, c_void};')
    w('')
    w('use crate::actor::ActorRef;')
    w('use crate::interop::{cpp_ref, register_cpp_lookup, register_inbound, resolve_local, NameBuf};')
    w('use crate::message::Message;')
    w('')
    w('pub const INTEROP_STRING_MAX: usize = 64;')
    w('')
    w('/// Wire form of a string (mirrors C `interop_string`): fixed buffer + length.')
    w('#[repr(C)]')
    w('#[derive(Clone, Copy)]')
    w('pub struct CInteropString {')
    w('    pub data: [u8; INTEROP_STRING_MAX],')
    w('    pub len: u32,')
    w('}')
    w('')
    w('impl Default for CInteropString {')
    w('    fn default() -> Self {')
    w('        CInteropString { data: [0u8; INTEROP_STRING_MAX], len: 0 }')
    w('    }')
    w('}')
    w('')
    w('impl CInteropString {')
    w('    fn from_str(s: &str) -> Self {')
    w('        let mut out = Self::default();')
    w('        let n = s.len().min(INTEROP_STRING_MAX - 1);')
    w('        out.data[..n].copy_from_slice(&s.as_bytes()[..n]);')
    w('        out.len = n as u32;')
    w('        out')
    w('    }')
    w('    fn decode(self) -> String {')
    w('        let n = (self.len as usize).min(INTEROP_STRING_MAX);')
    w('        String::from_utf8_lossy(&self.data[..n]).into_owned()')
    w('    }')
    w('}')
    w('')

    # per-message definitions
    for msg in messages:
        w(f'// ---- {msg.name} (id {msg.msg_id}) ' + '-' * max(0, 40 - len(msg.name)))
        # wire struct
        w('#[repr(C)]')
        w('#[derive(Clone, Copy)]')
        w(f'pub struct C{msg.name} {{')
        for f in msg.fields:
            w(f'    pub {f.name}: {rust_wire(f)},')
        w('}')
        w('')
        # native struct. `Default` is only derivable when every field is Default,
        # and Rust arrays impl Default only for length <= 32 — so skip it if a
        # field is a larger array (Clone/Debug work for any length).
        can_default = all(f.array_size is None or f.array_size <= 32 for f in msg.fields)
        w('#[derive(Clone, Debug, Default)]' if can_default else '#[derive(Clone, Debug)]')
        w(f'pub struct {msg.name} {{')
        for f in msg.fields:
            w(f'    pub {f.name}: {rust_native(f)},')
        w('}')
        w(f'crate::define_message!({msg.name}, {msg.msg_id});')
        w('')
        w(f'impl {msg.name} {{')
        # to_c
        w(f'    pub fn to_c(&self) -> C{msg.name} {{')
        w(f'        C{msg.name} {{')
        for f in msg.fields:
            if f.is_string:
                w(f'            {f.name}: CInteropString::from_str(&self.{f.name}),')
            elif f.is_bool:
                w(f'            {f.name}: if self.{f.name} {{ 1 }} else {{ 0 }},')
            else:
                w(f'            {f.name}: self.{f.name},')
        w('        }')
        w('    }')
        # from_c
        w(f'    pub fn from_c(c: &C{msg.name}) -> Self {{')
        w(f'        {msg.name} {{')
        for f in msg.fields:
            if f.is_string:
                w(f'            {f.name}: c.{f.name}.decode(),')
            elif f.is_bool:
                w(f'            {f.name}: c.{f.name} != 0,')
            else:
                w(f'            {f.name}: c.{f.name},')
        w('        }')
        w('    }')
        w('}')
        w('')

    # C++ bridge functions (implemented in C++, resolved at final link).
    w('// C++ bridge entry points — implemented in C++, resolved at final link time.')
    w('// The pure-Rust interop test links tiny stubs for these instead.')
    w('extern "C" {')
    w('    fn cpp_actor_send(actor: *const c_char, sender: *const c_char, id: c_int, data: *const c_void) -> c_int;')
    w('    fn cpp_actor_fast_send(actor: *const c_char, sender: *const c_char, id: c_int, data: *const c_void) -> c_int;')
    w('    fn cpp_actor_exists(actor: *const c_char) -> c_int;')
    w('}')
    w('')

    # inbound dispatch (C++ -> Rust)
    w('/// Inbound dispatch (C++ -> Rust): rebuild the message from its C struct and')
    w('/// deliver it to the named local actor. Registered as the framework `InboundFn`.')
    w('fn interop_inbound(name: &str, _sender: &str, msg_id: i32, data: *const c_void, fast: bool) -> i32 {')
    w('    let target = match resolve_local(name) {')
    w('        Some(t) => t,')
    w('        None => return -1, // no such local actor')
    w('    };')
    w('    match msg_id {')
    for msg in messages:
        w(f'        {msg.msg_id} => {{')
        w(f'            let c = unsafe {{ &*(data as *const C{msg.name}) }};')
        w(f'            let m = {msg.name}::from_c(c);')
        w('            if fast {')
        w('                target.fast_send(&m, None);')
        w('            } else {')
        w('                target.send(Box::new(m), None);')
        w('            }')
        w('            0')
        w('        }')
    w('        _ => -2, // unknown message id')
    w('    }')
    w('}')
    w('')

    # outbound marshalling (Rust -> C++), shared body for send + fast_send
    for kind, cfn in (('send', 'cpp_actor_send'), ('fast', 'cpp_actor_fast_send')):
        w(f'/// Outbound marshalling (Rust -> C++), {kind}. Registered via `cpp_ref`.')
        w(f'fn cpp_outbound_{kind}(target: &str, sender: &str, msg: &dyn Message) -> i32 {{')
        w('    let t = NameBuf::new(target);')
        w('    let s = NameBuf::new(sender);')
        w('    match msg.message_id() {')
        for msg in messages:
            w(f'        {msg.msg_id} => match msg.as_any().downcast_ref::<{msg.name}>() {{')
            w('            Some(m) => {')
            w('                let c = m.to_c();')
            w(f'                unsafe {{ {cfn}(t.as_ptr(), s.as_ptr(), {msg.msg_id}, &c as *const _ as *const c_void) }}')
            w('            }')
            w('            None => -2,')
            w('        },')
        w('        _ => -2,')
        w('    }')
        w('}')
        w('')

    # cpp lookup + register
    w('/// Resolve a name to a C++ actor over FFI (used by `Manager::get_ref`).')
    w('fn cpp_lookup(name: &str, sender: &str) -> Option<ActorRef> {')
    w('    let nb = NameBuf::new(name);')
    w('    if unsafe { cpp_actor_exists(nb.as_ptr()) } != 0 {')
    w('        Some(cpp_ref(name, sender, cpp_outbound_send, cpp_outbound_fast))')
    w('    } else {')
    w('        None')
    w('    }')
    w('}')
    w('')
    w('/// Install the generated inbound dispatcher and C++ resolver. Call once at')
    w('/// startup, after `register_local_lookup(...)`.')
    w('pub fn register() {')
    w('    register_inbound(interop_inbound);')
    w('    register_cpp_lookup(cpp_lookup);')
    w('}')
    w('')

    _write(out_path, '\n'.join(o))


# ---------------------------------------------------------------------------
# C++ emitter (targets public actors/cpp API; built with the hybrid examples)
# ---------------------------------------------------------------------------

_CPP = {  # C type -> C++ type
    'int32_t': 'int32_t', 'int64_t': 'int64_t', 'uint32_t': 'uint32_t',
    'uint64_t': 'uint64_t', 'double': 'double', 'float': 'float', 'char': 'char',
    'interop_string': 'std::string',
}


def cpp_type(f: Field) -> str:
    if f.is_bool:
        return 'bool'
    base = _CPP.get(f.c_type, f.c_type)
    return f'std::array<{base}, {f.array_size}>' if f.array_size else base


def generate_cpp_messages(messages: List[Message], cpp_dir: str) -> None:
    o: List[str] = []
    w = o.append
    w('// AUTO-GENERATED by interop/codegen/generate.py — DO NOT EDIT.')
    w('#pragma once')
    w('#include <string>')
    w('#include <array>')
    w('#include <cstring>')
    w('#include <algorithm>')
    w('#include "actors/Message.hpp"')
    w('#include "interop_messages.h"')
    w('')
    w('namespace msg {')
    w('')
    for msg in messages:
        w(f'struct {msg.name} : public actors::Message_N<{msg.msg_id}> {{')
        w(f'    static constexpr int ID = {msg.msg_id};')
        for f in msg.fields:
            w(f'    {cpp_type(f)} {f.name}{{}};')
        w('')
        w(f'    ::{msg.name} to_c() const {{')
        w(f'        ::{msg.name} c{{}};')
        for f in msg.fields:
            if f.is_string:
                w(f'        std::strncpy(c.{f.name}.data, {f.name}.c_str(), INTEROP_STRING_MAX - 1);')
                # len reflects the bytes actually copied (<= 63), never the full
                # untruncated size — keeps the wire self-consistent.
                w(f'        c.{f.name}.len = static_cast<uint32_t>('
                  f'std::min<size_t>({f.name}.size(), INTEROP_STRING_MAX - 1));')
            elif f.is_bool:
                w(f'        c.{f.name} = {f.name} ? 1 : 0;')
            elif f.array_size:
                w(f'        std::copy({f.name}.begin(), {f.name}.end(), c.{f.name});')
            else:
                w(f'        c.{f.name} = {f.name};')
        w('        return c;')
        w('    }')
        w('')
        w(f'    static {msg.name} from_c(const ::{msg.name}& c) {{')
        w(f'        {msg.name} m;')
        for f in msg.fields:
            if f.is_string:
                # Clamp the untrusted length to the buffer size (mirrors Rust's
                # decode) so a bad `len` can't read past the fixed 64-byte buffer.
                w(f'        m.{f.name} = std::string(c.{f.name}.data, '
                  f'std::min<uint32_t>(c.{f.name}.len, INTEROP_STRING_MAX));')
            elif f.is_bool:
                w(f'        m.{f.name} = c.{f.name} != 0;')
            elif f.array_size:
                w(f'        std::copy(std::begin(c.{f.name}), std::end(c.{f.name}), m.{f.name}.begin());')
            else:
                w(f'        m.{f.name} = c.{f.name};')
        w('        return m;')
        w('    }')
        w('};')
        w('')
    w('} // namespace msg')
    _write(os.path.join(cpp_dir, 'InteropMessages.hpp'), '\n'.join(o))


def generate_cpp_bridge(messages: List[Message], cpp_dir: str) -> None:
    # Header
    hdr = [
        '// AUTO-GENERATED by interop/codegen/generate.py — DO NOT EDIT.',
        '#pragma once',
        '#include <cstdint>',
        'namespace actors { class Manager; }',
        'extern "C" {',
        '    // Wire the C++ Manager whose actors Rust can reach by name.',
        '    void cpp_actor_init(actors::Manager* mgr);',
        '    void cpp_actor_shutdown();',
        '    int32_t cpp_actor_exists(const char* name);',
        '    int32_t cpp_actor_send(const char* actor, const char* sender, int32_t id, const void* data);',
        '    int32_t cpp_actor_fast_send(const char* actor, const char* sender, int32_t id, const void* data);',
        '}',
    ]
    _write(os.path.join(cpp_dir, 'CppActorBridge.hpp'), '\n'.join(hdr))

    # Implementation
    o: List[str] = []
    w = o.append
    w('// AUTO-GENERATED by interop/codegen/generate.py — DO NOT EDIT.')
    w('#include "CppActorBridge.hpp"')
    w('#include "InteropMessages.hpp"')
    w('#include "actors/act/Manager.hpp"')
    w('')
    w('namespace { actors::Manager* g_manager = nullptr; }')
    w('')
    w('extern "C" {')
    w('')
    w('void cpp_actor_init(actors::Manager* mgr) { g_manager = mgr; }')
    w('void cpp_actor_shutdown() { g_manager = nullptr; }')
    w('')
    # NOTE: actors::Manager::get_actor_by_name returns an ActorRef BY VALUE and
    # THROWS std::runtime_error when the name is unknown (Manager.cpp). So every
    # lookup is wrapped in try/catch: a C++ exception must never unwind across an
    # extern "C" frame, and "not found" must become a return code, not a throw.
    w('int32_t cpp_actor_exists(const char* name) {')
    w('    if (!name || !g_manager) return 0;')
    w('    try {')
    w('        return g_manager->get_actor_by_name(name).is_valid() ? 1 : 0;')
    w('    } catch (...) {')
    w('        return 0;')
    w('    }')
    w('}')
    w('')
    for fn, method in (('cpp_actor_send', 'send'), ('cpp_actor_fast_send', 'fast_send')):
        w(f'int32_t {fn}(const char* actor, const char* sender, int32_t id, const void* data) {{')
        w('    (void)sender; // reply routing: see Phase 3')
        w('    if (!actor || !data || !g_manager) return -1;')
        w('    try {')
        w('        actors::ActorRef target = g_manager->get_actor_by_name(actor);')
        w('        if (!target.is_valid()) return -1;')
        w('        switch (id) {')
        for msg in messages:
            w(f'            case {msg.msg_id}: {{')
            w(f'                msg::{msg.name} m = msg::{msg.name}::from_c('
              f'*static_cast<const ::{msg.name}*>(data));')
            if method == 'send':
                w(f'                target.send(new msg::{msg.name}(m), nullptr);')
            else:
                # fast_send returns the reply unique_ptr; dropped (no reply across
                # FFI). ActorRef::fast_send throws for non-local targets — the
                # surrounding try/catch turns that into -1.
                w('                target.fast_send(&m, nullptr);')
            w('                return 0;')
            w('            }')
        w('            default: return -2;')
        w('        }')
        w('    } catch (...) {')
        w('        return -1; // unknown actor / non-local fast_send / lookup failure')
        w('    }')
        w('}')
        w('')
    w('} // extern "C"')
    _write(os.path.join(cpp_dir, 'CppActorBridge.cpp'), '\n'.join(o))


def generate_rust_actor_if(messages: List[Message], cpp_dir: str) -> None:
    o: List[str] = []
    w = o.append
    w('// AUTO-GENERATED by interop/codegen/generate.py — DO NOT EDIT.')
    w('#pragma once')
    w('#include <string>')
    w('#include "InteropMessages.hpp"')
    w('')
    w('extern "C" {')
    w('    int32_t rust_actor_send(const char* actor, const char* sender, int32_t id, const void* data);')
    w('    int32_t rust_actor_fast_send(const char* actor, const char* sender, int32_t id, const void* data);')
    w('    int32_t rust_actor_exists(const char* name);')
    w('}')
    w('')
    w('namespace interop {')
    w('')
    w('// Send messages from C++ to a Rust actor. `sender` is attached for replies.')
    w('class RustActorIF {')
    w('public:')
    w('    RustActorIF(std::string actor, std::string sender = "")')
    w('        : actor_(std::move(actor)), sender_(std::move(sender)) {}')
    w('')
    w('    template <typename Msg> int send(const Msg& m) const {')
    w('        auto c = m.to_c();')
    w('        return rust_actor_send(actor_.c_str(), sender_.empty() ? nullptr : sender_.c_str(), Msg::ID, &c);')
    w('    }')
    w('    template <typename Msg> int fast_send(const Msg& m) const {')
    w('        auto c = m.to_c();')
    w('        return rust_actor_fast_send(actor_.c_str(), sender_.empty() ? nullptr : sender_.c_str(), Msg::ID, &c);')
    w('    }')
    w('    bool exists() const { return rust_actor_exists(actor_.c_str()) != 0; }')
    w('    const std::string& name() const { return actor_; }')
    w('')
    w('private:')
    w('    std::string actor_;')
    w('    std::string sender_;')
    w('};')
    w('')
    w('} // namespace interop')
    _write(os.path.join(cpp_dir, 'RustActorIF.hpp'), '\n'.join(o))


# ---------------------------------------------------------------------------

def _write(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not text.endswith('\n'):
        text += '\n'
    with open(path, 'w') as f:
        f.write(text)
    print(f'  wrote {path}')


def main() -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    crate_root = os.path.abspath(os.path.join(here, '..', '..'))
    header = os.path.join(crate_root, 'interop', 'messages', 'interop_messages.h')
    if len(sys.argv) == 3:
        header, crate_root = sys.argv[1], sys.argv[2]
    elif len(sys.argv) != 1:
        print(__doc__)
        sys.exit(1)

    print(f'Parsing {header}')
    messages = parse_header(header)
    for m in messages:
        fs = ', '.join(f'{f.name}:{f.c_type}{"[" + str(f.array_size) + "]" if f.array_size else ""}'
                       for f in m.fields)
        print(f'  {m.name} (id {m.msg_id}): {fs}')

    rust_out = os.path.join(crate_root, 'src', 'interop', 'generated.rs')
    cpp_dir = os.path.join(crate_root, 'interop', 'generated', 'cpp')
    print('Generating Rust:')
    generate_rust(messages, rust_out)
    print('Generating C++:')
    generate_cpp_messages(messages, cpp_dir)
    generate_cpp_bridge(messages, cpp_dir)
    generate_rust_actor_if(messages, cpp_dir)
    print(f'Done: {len(messages)} messages.')


if __name__ == '__main__':
    main()
