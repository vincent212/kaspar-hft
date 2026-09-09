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
# TESTED: the codegen half (jar download + `sbe.target.language=CPP` producing
# the repo's exact `_SBE_*_H_` / `SBE_CONSTEXPR` header style, one file per type,
# under a subdir named from the schema's `package` attribute). NOT tested here:
# the CME SFTP fetch and the exact remote template paths / version discovery —
# those need a CME-entitled network. The paths below are CME's standard SBEFix
# layout; VERIFY them on first run and adjust SCHEMAS if CME differs.

import argparse
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CACHE = os.path.join(HERE, ".cache")

# CME SFTP (same trusted host + public config credentials as genconfig/).
SFTP_HOST = "sftpng.cmegroup.com"
SFTP_USER = "cmeconfig"
SFTP_PASS = "G3t(0nnect3d"

ENV_DIR = {"prod": "Production", "nrcert": "NRCert", "cert": "Cert"}

# One entry per schema. `remote_dir` is the CME SFTP directory that holds the
# SBE template(s); `remote_file` the template filename. `out_pkg` is the repo
# subdir the headers land in — we rewrite the template's `package` attribute to
# this so the SBE tool emits into <repo>/<out_pkg>/.
#
# `pinned_version` is the SBE schema version the checked-in codecs were built and
# tested against (the `sbeSchemaVersion()` in the generated headers). The code
# depends on those struct/wire layouts, so by DEFAULT we regenerate that exact
# version and REFUSE to silently emit a different one — if CME's current template
# has moved on, generation stops with instructions rather than changing layouts
# under the code's feet. Use --latest (or --version) to intentionally move.
#
# VERIFY these remote paths against CME (they could not be probed from the dev
# box). MDP3's templates_FixBinary.xml lives under SBEFix/<Env>/Templates in the
# standard layout; iLink 3 publishes its own SBE template — confirm its exact
# path/filename and fix `remote_dir`/`remote_file` here if it differs.
SCHEMAS = {
    "mdp3": {
        "remote_dir": "SBEFix/{env}/Templates",
        "remote_file": "templates_FixBinary.xml",
        "out_pkg": "mktdata_v12",
        "pinned_version": 12,          # mktdata_v12: sbeSchemaId 1, sbeSchemaVersion 12
    },
    "ilink": {
        "remote_dir": "SBEFix/{env}/Templates",       # VERIFY: iLink 3 template location
        "remote_file": "templates_FixBinary_iLink3.xml",  # VERIFY: iLink 3 template name
        "out_pkg": "ilink_v8",
        "pinned_version": 8,           # ilink_v8: sbeSchemaId 8, sbeSchemaVersion 8
    },
}

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


def sftp_open():
    try:
        import paramiko
    except ImportError:
        sys.exit("[genschema] ERROR: paramiko is required for SFTP fetch "
                 "(`pip install paramiko`), or pass --template-file to skip it")
    socket.setdefaulttimeout(30)
    t = paramiko.Transport((SFTP_HOST, 22))
    t.connect(username=SFTP_USER, password=SFTP_PASS)
    return t, t.open_sftp_client()


def resolve_remote_file(sftp, remote_dir, remote_file, version, latest):
    """Pick the template file on CME for the requested version / latest."""
    if not latest and version is None:
        return f"{remote_dir}/{remote_file}"
    entries = sftp.listdir(remote_dir)
    stem, ext = os.path.splitext(remote_file)
    # Versioned templates are conventionally <stem>.<version><ext> or carry the
    # version in the name; match those, else fall back to the plain file.
    cands = [e for e in entries if e.startswith(stem) and e.endswith(ext)]
    if not cands:
        sys.exit(f"[genschema] no template matching '{stem}*{ext}' in {remote_dir}: {entries}")
    mtime = lambda e: sftp.stat(f"{remote_dir}/{e}").st_mtime
    if version is not None:
        # Match the version as a whole number token, not a bare substring, so
        # e.g. --version 1 doesn't match "v11"/"12". Among matches pick the
        # newest by mtime (deterministic; lexicographic sort mis-orders 9 vs 11).
        tok = re.compile(rf"(?<!\d){re.escape(str(version))}(?!\d)")
        match = [e for e in cands if tok.search(e)]
        if not match:
            sys.exit(f"[genschema] no template for version {version} in {remote_dir}: {cands}")
        return f"{remote_dir}/{max(match, key=mtime)}"
    # --latest: newest by mtime
    latest_e = max(cands, key=mtime)
    log(f"latest template in {remote_dir}: {latest_e}")
    return f"{remote_dir}/{latest_e}"


def schema_version_of(xml_path):
    """Read the SBE schema version attribute from <sbe:messageSchema ...>."""
    with open(xml_path, encoding="utf-8") as f:
        xml = f.read()
    m = re.search(r'<sbe:messageSchema\b[^>]*?\bversion="(\d+)"', xml)
    return int(m.group(1)) if m else None


def set_package(xml_path, pkg):
    """Force the schema's package attribute so the SBE tool emits into <pkg>/."""
    with open(xml_path, encoding="utf-8") as f:
        xml = f.read()
    new, n = re.subn(r'(<sbe:messageSchema\b[^>]*?\bpackage=")[^"]*(")',
                     rf'\g<1>{pkg}\g<2>', xml, count=1)
    if n == 0:  # no package attribute present -> inject one
        new, n = re.subn(r'(<sbe:messageSchema\b)', rf'\g<1> package="{pkg}"', xml, count=1)
    if n == 0:
        sys.exit(f"[genschema] could not find <sbe:messageSchema> in {xml_path}")
    with open(xml_path, "w", encoding="utf-8") as f:
        f.write(new)


def generate(jar, template_xml, out_pkg):
    out_dir = os.path.join(REPO, out_pkg)
    os.makedirs(out_dir, exist_ok=True)
    # Clear stale generated headers so a renamed/removed message can't linger
    # (mirrors the rm-before-ar fix in actors/cpp/Makefile). Remove only *.h —
    # the dir also holds the tracked README.md and .gitignore, which the SBE
    # tool does not regenerate, so an rmtree of the whole dir would delete them.
    for h in glob.glob(os.path.join(out_dir, "*.h")):
        os.remove(h)
    log(f"generating {out_pkg}/ from {os.path.basename(template_xml)}")
    subprocess.run(
        ["java", "-Dsbe.target.language=CPP", f"-Dsbe.output.dir={REPO}", "-jar", jar, template_xml],
        check=True)
    n = len(glob.glob(os.path.join(out_dir, "*.h")))
    if n == 0:
        sys.exit(f"[genschema] SBE tool produced no headers in {out_dir} "
                 f"(is the template's package attribute '{out_pkg}'?)")
    log(f"wrote {n} headers to {out_pkg}/")


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
            set_package(local, s["out_pkg"])
            generate(jar, local, s["out_pkg"])
            return

        transport, sftp = sftp_open()
        try:
            for name in targets:
                s = SCHEMAS[name]
                remote_dir = s["remote_dir"].format(env=ENV_DIR[args.env])
                remote = resolve_remote_file(sftp, remote_dir, s["remote_file"],
                                             args.version, args.latest)
                local = os.path.join(tmp, f"{name}.xml")
                log(f"downloading {remote}")
                sftp.get(remote, local)
                # Default (no --version/--latest): refuse to regenerate a schema
                # version other than the one the code was built against.
                if not args.latest and args.version is None:
                    pinned = s["pinned_version"]
                    got = schema_version_of(local)
                    if got is not None and got != pinned:
                        sys.exit(
                            f"[genschema] CME's current {name} template is schema version "
                            f"{got}, but this repo is built against version {pinned} "
                            f"({s['out_pkg']}). Regenerating would change the wire/struct "
                            f"layout the code depends on. Fetch the archived v{pinned} "
                            f"template and pass --template-file, or pass --latest to move "
                            f"the repo to v{got} deliberately.")
                set_package(local, s["out_pkg"])
                generate(jar, local, s["out_pkg"])
        finally:
            sftp.close()
            transport.close()

    log("done")


if __name__ == "__main__":
    main()
