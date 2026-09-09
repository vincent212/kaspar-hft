<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# ilink_v8
**Generated — do not edit or commit these headers.** The `*.h` here are the CME
iLink 3 SBE order-entry codecs, produced by the real-logic SBE tool from CME's
iLink 3 `ilinkbinary.xml` schema (MSGW). They are git-ignored; regenerate with:

```bash
make schema                 # from the repo root — both schemas, pinned versions
# or just this one, a specific version (default is the pinned v8):
python3 genschema/genschema.py --schema ilink --version <VER>
```

See [`genschema/README.md`](../genschema/README.md).
