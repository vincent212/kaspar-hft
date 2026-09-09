#!/usr/bin/env python3

# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Generate the CME SBE message codecs (mktdata_v12/ = MDP3, ilink_v8/ = iLink 3)
# from CME's published SBE templates, instead of committing the generated
# headers. Fetches templates_FixBinary.xml from CME's SFTP server (same host and
# credentials as genconfig/), then runs the real-logic SBE tool's C++ generator.
#
#   python3 genschema.py [--schema mdp3|ilink|all] [--env prod|nrcert|cert]
#                        [--version VER | --latest] [--sbe-version 1.30.0]
#                        [--template-file PATH]   # skip SFTP, use a local XML
#
# Prerequisites: Java (for the SBE jar), Python `paramiko` (for SFTP), network
# access to Maven Central (to fetch the jar, cached after first run) and to CME
# SFTP (for the templates). `--template-file` bypasses SFTP if you already have
# the XML.
#
# Verified end-to-end against CME's production SFTP: MDP3 v12 and iLink 3 v8 were
# regenerated from CME and the full library build compiles against them (0
# errors). The SFTP paths, the version pinning, and the `package`->`sbe` rewrite
# all work. NOTE: output is functionally equivalent but NOT byte-identical to the
# headers previously committed to git — those were produced by a different
# real-logic SBE version (different include ordering / wrapForEncode style) and
# carried a hand-added license header. The struct layout, `sbe` namespace, guard
# style, and API match. Pass --sbe-version to match a specific prior generation.

import argparse
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CACHE = os.path.join(HERE, ".cache")

# CME SFTP (same trusted host + public config credentials as genconfig/).
SFTP_HOST = "sftpng.cmegroup.com"
SFTP_USER = "cmeconfig"
SFTP_PASS = "G3t(0nnect3d"

ENV_DIR = {"prod": "Production", "nrcert": "NRCert", "cert": "Cert"}

# One entry per schema, verified against CME's SFTP layout:
#   MDP3 market data  -> SBEFix/<Env>/Templates/  (package "mktdata", id 1)
#   iLink 3 order entry -> MSGW/<Env>/Templates/  (package "iLinkBinary", id 8)
# `latest_*` is the current published template; versioned templates have
# deterministic names under `version_dir` (MDP3 archives old versions under
# .../Templates/Archive/; iLink keeps them alongside the current one).
# `out_pkg` is the repo subdir the headers land in — we rewrite the template's
# `package` attribute to this so the SBE tool emits into <repo>/<out_pkg>/.
#
# `pinned_version` is the SBE schema version the checked-in codecs were built and
# tested against (the `sbeSchemaVersion()` in the generated headers). The code
# depends on those struct/wire layouts, so by DEFAULT we regenerate that exact
# version (CME's current templates have already moved past it: MDP3 v13, iLink
# v9). Use --latest to pull CME's current template, or --version N for another.
SCHEMAS = {
    "mdp3": {
        "latest_dir": "SBEFix/{env}/Templates",
        "latest_file": "templates_FixBinary.xml",
        "version_dir": "SBEFix/{env}/Templates/Archive",
        "version_file": "templates_FixBinary_v{ver}.xml",
        "out_pkg": "mktdata_v12",
        "pinned_version": 12,          # mktdata_v12: sbeSchemaId 1, sbeSchemaVersion 12
    },
    "ilink": {
        "latest_dir": "MSGW/{env}/Templates",
        "latest_file": "ilinkbinary.xml",
        "version_dir": "MSGW/{env}/Templates",
        "version_file": "ilinkbinary_v{ver}.xml",
        "out_pkg": "ilink_v8",
        "pinned_version": 8,           # ilink_v8: sbeSchemaId 8, sbeSchemaVersion 8
    },
}

# The checked-in codecs live in namespace `sbe` (the code refers to the types as
# sbe::NewOrderSingle514 etc.), regardless of which directory they sit in. CME's
# templates declare package "mktdata" / "iLinkBinary"; we rewrite that to `sbe`
# so the generated types land in the namespace the code expects. The per-schema
# directory (mktdata_v12/ ilink_v8/) is the include path, decoupled from the
# package — generate() puts the headers there directly.
SBE_PACKAGE = "sbe"

DEFAULT_SBE_VERSION = "1.30.0"
MAVEN = "https://repo1.maven.org/maven2/uk/co/real-logic/sbe-all/{v}/sbe-all-{v}.jar"


def log(msg):
    print(f"[genschema] {msg}", file=sys.stderr, flush=True)


def fetch_jar(version):
    os.makedirs(CACHE, exist_ok=True)
    jar = os.path.join(CACHE, f"sbe-all-{version}.jar")
    if os.path.exists(jar) and os.path.getsize(jar) > 0:
        log(f"using cached {jar}")
        return jar
    url = MAVEN.format(v=version)
    log(f"downloading {url}")
    with urllib.request.urlopen(url, timeout=120) as r, open(jar, "wb") as f:
        shutil.copyfileobj(r, f)
    if os.path.getsize(jar) == 0:
        raise RuntimeError(f"downloaded empty jar from {url}")
    return jar


def sftp_open(tries=4):
    try:
        import paramiko
    except ImportError:
        sys.exit("[genschema] ERROR: paramiko is required for SFTP fetch "
                 "(`pip install paramiko`), or pass --template-file to skip it")
    socket.setdefaulttimeout(40)
    last = None
    for i in range(tries):
        t = None
        try:
            t = paramiko.Transport((SFTP_HOST, 22))
            t.connect(username=SFTP_USER, password=SFTP_PASS)
            return t, t.open_sftp_client()
        except Exception as e:  # CME occasionally drops the handshake; retry
            last = e
            if t is not None:
                try:
                    t.close()  # don't leak the half-open transport across retries
                except Exception:
                    pass
            log(f"SFTP connect failed ({e}); retry {i + 1}/{tries}")
            time.sleep(5)
    sys.exit(f"[genschema] ERROR: could not connect to {SFTP_HOST}: {last}")


# The CME templates use an arbitrary namespace prefix on messageSchema (e.g.
# <ns2:messageSchema ...>), so match any "<prefix:messageSchema" / "<messageSchema".
_SCHEMA_TAG = r'<(?:[\w.-]+:)?messageSchema\b'


def remote_template_path(schema, env, version, latest):
    """Deterministic CME SFTP path for the requested schema/version/latest."""
    s = SCHEMAS[schema]
    if latest:
        return s["latest_dir"].format(env=env) + "/" + s["latest_file"]
    ver = s["pinned_version"] if version is None else version
    return s["version_dir"].format(env=env) + "/" + s["version_file"].format(ver=ver)


def schema_version_of(xml_path):
    """Read the SBE schema version attribute from the <...messageSchema ...> tag."""
    with open(xml_path, encoding="utf-8") as f:
        xml = f.read()
    m = re.search(_SCHEMA_TAG + r'[^>]*?\bversion="(\d+)"', xml)
    return int(m.group(1)) if m else None


def set_package(xml_path, pkg):
    """Force the schema's package attribute so the SBE tool emits into <pkg>/."""
    with open(xml_path, encoding="utf-8") as f:
        xml = f.read()
    new, n = re.subn(r'(' + _SCHEMA_TAG + r'[^>]*?\bpackage=")[^"]*(")',
                     rf'\g<1>{pkg}\g<2>', xml, count=1)
    if n == 0:  # no package attribute present -> inject one
        new, n = re.subn(r'(' + _SCHEMA_TAG + r')', rf'\g<1> package="{pkg}"', xml, count=1)
    if n == 0:
        sys.exit(f"[genschema] could not find a messageSchema element in {xml_path}")
    with open(xml_path, "w", encoding="utf-8") as f:
        f.write(new)


def generate(jar, template_xml, out_pkg):
    out_dir = os.path.join(REPO, out_pkg)
    os.makedirs(out_dir, exist_ok=True)
    log(f"generating {out_pkg}/ from {os.path.basename(template_xml)}")
    # The SBE tool emits into <output.dir>/<package-as-path> (here <tmp>/sbe/),
    # so generate into a temp dir and copy the headers into out_pkg/. This keeps
    # the include directory (mktdata_v12/, ilink_v8/) independent of the `sbe`
    # package/namespace.
    with tempfile.TemporaryDirectory() as td:
        subprocess.run(
            ["java", "-Dsbe.target.language=CPP", f"-Dsbe.output.dir={td}", "-jar", jar, template_xml],
            check=True)
        gen = glob.glob(os.path.join(td, "**", "*.h"), recursive=True)
        if not gen:
            sys.exit(f"[genschema] SBE tool produced no headers for {out_pkg}/")
        # Clear stale generated headers so a renamed/removed message can't linger
        # (mirrors the rm-before-ar fix in actors/cpp/Makefile). Remove only *.h —
        # the dir also holds the tracked README.md and .gitignore.
        for h in glob.glob(os.path.join(out_dir, "*.h")):
            os.remove(h)
        for h in gen:
            shutil.copy(h, out_dir)
    log(f"wrote {len(gen)} headers to {out_pkg}/")


def main():
    ap = argparse.ArgumentParser(description="Generate CME SBE codecs from CME templates")
    ap.add_argument("--schema", choices=["mdp3", "ilink", "all"], default="all")
    ap.add_argument("--env", choices=list(ENV_DIR), default="prod")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--version", help="CME schema version to fetch")
    g.add_argument("--latest", action="store_true", help="fetch the newest template on CME")
    ap.add_argument("--sbe-version", default=DEFAULT_SBE_VERSION, help="real-logic sbe-all jar version")
    ap.add_argument("--template-file", help="use a local template XML instead of SFTP "
                                            "(only valid with a single --schema)")
    args = ap.parse_args()

    targets = list(SCHEMAS) if args.schema == "all" else [args.schema]
    if args.template_file and len(targets) != 1:
        sys.exit("[genschema] --template-file requires a single --schema mdp3|ilink")

    jar = fetch_jar(args.sbe_version)

    with tempfile.TemporaryDirectory() as tmp:
        if args.template_file:
            s = SCHEMAS[targets[0]]
            local = os.path.join(tmp, "template.xml")
            shutil.copyfile(args.template_file, local)
            got = schema_version_of(local)
            if got is not None and got != s["pinned_version"]:
                log(f"WARNING: {args.template_file} is schema version {got}, but "
                    f"{s['out_pkg']} is pinned to v{s['pinned_version']}; the code may "
                    f"not match the regenerated layout.")
            set_package(local, SBE_PACKAGE)
            generate(jar, local, s["out_pkg"])
            return

        env = ENV_DIR[args.env]
        transport, sftp = sftp_open()
        try:
            for name in targets:
                s = SCHEMAS[name]
                remote = remote_template_path(name, env, args.version, args.latest)
                local = os.path.join(tmp, f"{name}.xml")
                log(f"downloading {remote}")
                try:
                    sftp.get(remote, local)
                except IOError:
                    sys.exit(f"[genschema] {name}: template not found on CME: {remote} "
                             f"(check --env/--version, or --latest for the current template)")
                # Sanity-check we got the version we asked for. Default and
                # --version have a definite expected version; --latest does not.
                expect = None if args.latest else (args.version or s["pinned_version"])
                got = schema_version_of(local)
                if expect is not None and got is not None and str(got) != str(expect):
                    sys.exit(f"[genschema] {name}: fetched schema version {got}, expected "
                             f"{expect} ({remote}) — CME may have changed its layout.")
                set_package(local, SBE_PACKAGE)
                generate(jar, local, s["out_pkg"])
        finally:
            sftp.close()
            transport.close()

    log("done")


if __name__ == "__main__":
    main()
