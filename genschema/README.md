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
2. Fetch `templates_FixBinary.xml` from CME SFTP for the chosen env/version
   (or use `--template-file`).
3. On the default path, verify the template's `sbeSchemaVersion` matches the
   pinned version and abort if not.
4. Rewrite the schema's `package` attribute to `mktdata_v12` / `ilink_v8` so the
   tool emits into the right repo subdir.
5. Regenerate that subdir with `java -Dsbe.target.language=CPP
   -Dsbe.output.dir=<repo> -jar sbe-all.jar <template.xml>`, removing stale
   `*.h` first (the tracked `README.md` / `.gitignore` are left in place).

## Status / caveats

- **Validated:** the codegen path — jar download, the `package` rewrite, and
  `sbe.target.language=CPP` producing the repo's exact header style
  (`_SBE_*_H_`, `SBE_CONSTEXPR`, one file per type) — using `--template-file`.
- **Not validated here (needs a CME-entitled network):** the SFTP fetch, the
  exact CME remote template paths, and `--latest` version discovery. The paths
  in `SCHEMAS` (top of `genschema.py`) are CME's standard SBEFix layout; the
  **iLink 3 template path/filename in particular is a placeholder — verify it on
  first run** and adjust `SCHEMAS["ilink"]` if CME differs.
- **Version match:** the currently-used real-logic SBE version isn't pinned in
  history; the header *format* is stable across versions, but if you need output
  byte-identical to a prior generation, pass the matching `--sbe-version`.
