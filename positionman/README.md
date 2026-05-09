<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# positionman — Position Manager

Tracks net positions per instrument. Receives `AddToPos` messages from lights on fills and maintains running position via `PCoord` references.

## Actor

| Actor | Purpose |
|-------|---------|
| `PositionManager` | Aggregates positions across all instruments |

## Messages

| Message | Purpose |
|---------|---------|
| `AddToPos` | Increment/decrement position for an instrument |
| `GetPos` | Request current position |
| `Pos` | Position reply |

## Build

```bash
cd positionman/src && KSPRPROJ=~/m2_kaspar make
```
