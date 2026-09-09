<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# genschema — generate the CME SBE codecs

`mktdata_v12/` (MDP 3.0 market data) and `ilink_v8/` (iLink 3 order entry) are the
CME SBE message codecs. They are **generated, not committed** — `genschema.py`
fetches CME's `templates_FixBinary.xml` and runs the [real-logic SBE
tool](https://github.com/real-logic/simple-binary-encoding) to emit the C++
headers. The `*.h` are git-ignored (each dir keeps only its `README.md`).

## Pinned versions

The checked-in codecs were built and tested against **MDP3 schema v12**
(`mktdata_v12/`) and **iLink 3 schema v8** (`ilink_v8/`) — the
`sbeSchemaVersion()` in the generated headers. The rest of the code depends on
those exact struct/wire layouts, so **`make schema` regenerates those versions
by default and refuses to emit a different one**: if CME's current template has
moved to a newer version, generation stops with instructions rather than
silently changing layouts under the code. Move deliberately with `--latest` or
`--version`.

## Usage

```bash
# from the repo root — regenerate the pinned versions (MDP3 v12, iLink v8)
make schema

# deliberately move a schema to a different / the newest CME version:
python3 genschema/genschema.py --schema mdp3 --version 13
python3 genschema/genschema.py --schema mdp3 --latest

# one schema only:
python3 genschema/genschema.py --schema mdp3
python3 genschema/genschema.py --schema ilink

# no CME access — generate from a template XML you already have:
python3 genschema/genschema.py --schema mdp3 --template-file path/to/templates_FixBinary.xml
```

`make` refuses to build if the codecs are missing and tells you to run
`make schema` (see the `check-schema` target).

## Prerequisites

- **Java** — runs the SBE jar (`sbe-all-<ver>.jar`, auto-downloaded from Maven
  Central to `genschema/.cache/` on first use, then cached).
- **Python `paramiko`** — for the CME SFTP fetch (`pip install paramiko`). Not
  needed with `--template-file`.
- **Network** — Maven Central (jar) and CME SFTP (templates).
- **CME access** — the SFTP host/credentials are the same public config
  credentials `genconfig/` uses (`cmeconfig` on `sftpng.cmegroup.com`).

## Options

| flag | meaning | default |
|---|---|---|
| `--schema mdp3\|ilink\|all` | which codec(s) to generate | `all` |
| `--env prod\|nrcert\|cert` | CME environment | `prod` |
| `--version VER` | fetch a specific CME schema version | pinned (MDP3 v12 / iLink v8) |
| `--latest` | fetch the newest template on CME | — |
| `--sbe-version X` | real-logic `sbe-all` jar version | `1.30.0` |
| `--template-file PATH` | use a local XML, skip SFTP (single `--schema`) | — |

## How it works

1. Ensure `sbe-all-<ver>.jar` (download + cache).
2. Fetch the template from CME SFTP for the chosen schema/env/version — MDP3 from
   `SBEFix/<Env>/Templates` (archived versions under `Archive/`), iLink 3 from
   `MSGW/<Env>/Templates` (or use `--template-file`).
3. Verify the fetched template's `sbeSchemaVersion` matches the requested/pinned
   version and abort if not.
4. Rewrite the schema's `package` attribute to `sbe` (the namespace the code
   uses: `sbe::NewOrderSingle514`, …).
5. Run `java -Dsbe.target.language=CPP -jar sbe-all.jar <template.xml>` into a
   temp dir and copy the headers into `mktdata_v12/` / `ilink_v8/`, removing
   stale `*.h` first (the tracked `README.md` / `.gitignore` are left in place).

## Status / caveats

- **Verified end-to-end against CME production SFTP:** both schemas fetched at
  their pinned versions (`SBEFix/.../Archive/templates_FixBinary_v12.xml`,
  `MSGW/.../ilinkbinary_v8.xml`), regenerated, and the full library build
  compiles against them (0 errors) — types in `namespace sbe`, `_SBE_*_H_`
  guards, one file per type.
- **Not byte-identical to the headers previously committed to git.** Those were
  produced by a different real-logic SBE version (different include ordering /
  `wrapForEncode` style) and carried a hand-added license header. The struct
  layout, namespace, guards and API are the same, so the code is unaffected; the
  generated output is now the source of truth. Pass `--sbe-version` to match a
  specific prior generation if you need a closer diff.
- **CME's current templates have moved past the pins** (MDP3 v13, iLink v9), so
  `make schema` regenerates the pinned v12/v8 by default; use `--latest` to move.
