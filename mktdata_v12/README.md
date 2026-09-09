<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# mktdata_v12

**Generated — do not edit or commit these headers.** The `*.h` here are the CME
MDP 3.0 SBE market-data codecs, produced by the real-logic SBE tool from CME's
`templates_FixBinary.xml`. They are git-ignored; regenerate with:

```bash
make schema                 # from the repo root — both schemas, latest
# or just this one, a specific version:
python3 genschema/genschema.py --schema mdp3 --version <VER>
```

See [`genschema/README.md`](../genschema/README.md).
