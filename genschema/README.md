<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# genschema — generate the CME SBE codecs

`mdp3_sbe/` (MDP 3.0 market data) and `ilink3_sbe/` (iLink 3 order entry) are the
CME SBE message codecs. They are **generated, not committed** — `genschema.py`
fetches CME's `templates_FixBinary.xml` and runs the [real-logic SBE
tool](https://github.com/real-logic/simple-binary-encoding) to emit the C++
headers. The `*.h` are git-ignored (each dir keeps only its `README.md`).

## Versions

**`make schema` fetches CME's latest published templates** for both schemas
(`templates_FixBinary.xml` for MDP3, `ilinkbinary.xml` for iLink 3) and logs the
schema version it got. Use `--version N` to generate a specific archived
version instead.

## Usage

```bash
# from the repo root — generate both from CME's latest templates
make schema

# a specific CME schema version instead of the latest:
python3 genschema/genschema.py --schema mdp3 --version 12

# one schema only:
python3 genschema/genschema.py --schema mdp3
python3 genschema/genschema.py --schema ilink

# no CME access — generate from a template XML you already have
# (./build.sh does this automatically for templates_FixBinary.xml /
#  ilinkbinary.xml placed in genschema/ — see "No CME access" in CLAUDE.md):
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
| `--version VER` | fetch a specific CME schema version | latest |
| `--sbe-version X` | real-logic `sbe-all` jar version | `1.30.0` |
| `--template-file PATH` | use a local XML, skip SFTP (single `--schema`) | — |

## How it works

1. Ensure `sbe-all-<ver>.jar` (download + cache).
2. Fetch the template from CME SFTP for the chosen schema/env — the latest from
   `SBEFix/<Env>/Templates` (MDP3) and `MSGW/<Env>/Templates` (iLink 3), or with
   `--version` the archived one (MDP3 under `Archive/`). Or use `--template-file`.
3. With `--version`, verify the fetched template's `sbeSchemaVersion` matches
   and abort if not.
4. Rewrite the schema's `package` attribute to `sbe` (the namespace the code
   uses: `sbe::NewOrderSingle514`, …).
5. Run `java -Dsbe.target.language=CPP -jar sbe-all.jar <template.xml>` into a
   temp dir and copy the headers into `mdp3_sbe/` / `ilink3_sbe/`, removing
   stale `*.h` first (the tracked `README.md` / `.gitignore` are left in place).

## Status / caveats

- **Not byte-identical to the headers previously committed to git.** Those were
  produced by a different real-logic SBE version (different include ordering /
  `wrapForEncode` style) and carried a hand-added license header. The struct
  layout, namespace, guards and API are the same, so the code is unaffected; the
  generated output is now the source of truth. Pass `--sbe-version` to match a
  specific prior generation if you need a closer diff.
