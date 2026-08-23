<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# setclassid - Message ID Uniqueness Checker

## Overview

This script validates that all CFSM message IDs are unique across the entire codebase. Every CFSM message inherits from `cfsm::Message_N<ID>` where `ID` must be a unique integer between 0 and 511.

## Why Message IDs Must Be Unique

CFSM (Communicating Finite State Machine) uses message IDs for:
1. **Message dispatching**: Routing incoming messages to correct handlers
2. **Serialization**: Identifying message types in network/IPC communication
3. **Type safety**: Runtime type identification for message casting

**If two messages share the same ID, the system will have undefined behavior** - messages may be routed to the wrong handlers or deserialized as the wrong type.

## Message ID Constraints

- **Range**: 0 to 511 (inclusive)
- **Uniqueness**: Each ID must be used by exactly ONE message type
- **Hardcoded**: IDs are compile-time constants (template parameters)

## How to Run

### Prerequisites

Set `KSPRPROJ` to the repository root (optional — defaults to the repo the script lives in):
```bash
export KSPRPROJ=/path/to/kaspar-hft
```

### Basic Usage

```bash
cd $KSPRPROJ/setclassid
python3 setclassid.py
```

### Expected Output (Success)

```
$KSPRPROJ
$KSPRPROJ/actors/cpp/include/actors/msg/AddActor.hpp 11
$KSPRPROJ/actors/cpp/include/actors/msg/ActorRemoved.hpp 12
$KSPRPROJ/frame_ref/include/frame/mda/msg/Data.hpp 17
$KSPRPROJ/mcast_recv/include/mcast_recv/msg/ProcessQ.hpp 129
$KSPRPROJ/actors/cpp/include/actors/console/ConsoleMessages.hpp 200
...
max is 511
[0, 1, 2, 4, 5, ...]

=== MESSAGE ID ALLOCATION SUMMARY ===
Total IDs used: 132/512
Total IDs available: 378
Reserved IDs (0, 3): 2

Free slots: [2, 13, 14, 29, 30, 43, ...]
```

This shows each message file and its ID, the highest ID in use, and — most
useful when adding a message — the **free slots** you can pick a new ID from.
(Exact counts are a snapshot; run the script for current numbers.)

### Output on Error (Duplicate ID)

If a new message reuses an ID that's already taken (here `22`, already used by
`frame/ob/msg/BBBOChg.hpp`), the script prints the collision and aborts:

```
$KSPRPROJ/mtd/include/mtd/msg/YourNewMsg.hpp 22
22 *** IS USED MORE THAN ONCE *** DUPLICATE *** $KSPRPROJ/mtd/include/mtd/msg/YourNewMsg.hpp
   Used IDs so far: [0, 1, 2, ..., 22, ...]
   FREE SLOTS (examples): [2, 13, 14, 29, 30, ...]
Traceback (most recent call last):
  File ".../setclassid/setclassid.py", line 121, in <module>
    assert msgidx not in msgid
AssertionError
```

Pick one of the reported free slots instead.

### Output on Error (ID Out of Range)

IDs must be in `[0, 511]`; anything larger aborts:

```
$KSPRPROJ/mtd/include/mtd/msg/YourNewMsg.hpp 513
Traceback (most recent call last):
  File ".../setclassid/setclassid.py", line 122, in <module>
    assert msgidx < 512
AssertionError
```

## How to Assign New Message IDs

### Step 1: Run the Script to Find Available IDs

```bash
cd $KSPRPROJ/setclassid
python3 setclassid.py
```

Look at the output line:
```
max is 234
```

This means IDs 0-234 are used, and 235-511 are available.

### Step 2: Check for Gaps (Optional)

Look at the sorted list:
```
[0, 1, 2, 3, 4, 6, 7, 8, ...]
```

If there's a gap (e.g., missing 5), you can reuse that ID. However, **it's safer to use the next available ID after max** to avoid conflicts with in-progress work.

### Step 3: Assign IDs to New Messages

**Example**: Adding 6 new messages, and current max is 234.

Assign IDs 235-240:

```cpp
// mtd/include/mtd/msg/GetBBBO.hpp
struct GetBBBO : public cfsm::Message_N<235> {
    std::string cusip;
    uint64_t request_id;
};

// bbg/include/bbg/msg/GetPrice.hpp
struct GetPrice : public cfsm::Message_N<236> {
    std::string cusip;
    uint64_t request_id;
};

// bbg/include/bbg/msg/PriceInfo.hpp
struct PriceInfo : public cfsm::Message_N<237> {
    std::string cusip;
    double bid_px;
    double ask_px;
    double dv01;
    int status;
    uint64_t request_id;
};

// offtrpx/include/offtrpx/msg/GetOffTrPrice.hpp
struct GetOffTrPrice : public cfsm::Message_N<238> {
    std::string cusip;
    uint64_t request_id;
};

// offtrpx/include/offtrpx/msg/OffTrPriceInfo.hpp
struct OffTrPriceInfo : public cfsm::Message_N<239> {
    std::string cusip;
    double mid_px;
    int status;
    uint64_t request_id;
};

// polonaise/include/polonaise/mth/msg/RiskQueryResponse.hpp
struct RiskQueryResponse : public cfsm::Message_N<240> {
    std::string json_data;
    uint64_t request_id;
};
```

### Step 4: Verify Uniqueness

Run the script again:
```bash
python3 setclassid.py
```

Expected output:
```
max is 240
[0, 1, 2, ..., 234, 235, 236, 237, 238, 239, 240]
```

No assertion errors = success!

## Common Workflows

### Adding a New Message Type

1. Create the message `.hpp` file
2. Run `setclassid.py` to find max ID
3. Assign new message ID = max + 1
4. Run `setclassid.py` again to verify
5. Commit both the new message and updated max ID

### Resolving Duplicate ID Conflicts

If the script reports a duplicate:

1. Identify which message was added most recently
2. Run `setclassid.py` to find next available ID
3. Update the new message to use available ID
4. Run `setclassid.py` to verify fix
5. Commit the fix

### Finding All Messages for a Specific Component

```bash
cd $KSPRPROJ/setclassid
python3 setclassid.py | grep "bbg/include/bbg/msg"
```

This shows all BBG message IDs.

## Integration with Build System

**Recommendation**: Add this check to the build system to catch duplicate IDs before compilation.

Example Makefile integration:
```makefile
.PHONY: check-msgids
check-msgids:
	@cd $KSPRPROJ/setclassid && python3 setclassid.py > /dev/null && echo "✓ Message IDs are unique"

build: check-msgids
	# ... rest of build
```

## Troubleshooting

### Error: "KeyError: 'KSPRPROJ'"

**Cause**: Environment variable not set.

**Fix**:
```bash
export KSPRPROJ=/path/to/kaspar-hft
```

Or add to `~/.bashrc` for persistence.

### Error: "No such file or directory: '/interface'"

**Cause**: `KSPRPROJ` set to wrong path.

**Fix**: Ensure it points to the m2 root (not a subdirectory).

### Script Takes Long Time

**Cause**: Scanning thousands of .hpp files.

**Normal**: Script should complete in 1-5 seconds for typical codebase.

**If > 10 seconds**: Check for network-mounted directories or very large file trees.

## Message ID Allocation Strategy

### Current Convention

Based on analysis of existing messages, components tend to use ID ranges:

- **0-19**: Core CFSM messages
- **20-49**: Database/SQL messages
- **50-99**: Trading protocol messages (FIX, etc.)
- **100-149**: Market data messages
- **150-199**: Hedging/position messages
- **200-249**: RFQ/pricing messages
- **250-299**: Post-trade messages
- **300-349**: Risk/analytics messages
- **350-511**: Reserved for future use

**Note**: This is NOT enforced by the system, just a convention. The only hard rule is **uniqueness**.

### Recommendation for New Components

When adding a new component with multiple messages:
1. Find the current max ID
2. Reserve a block of 10-20 IDs for the component
3. Document the reservation (e.g., in component README)
4. Assign IDs sequentially within the block

Example:
```
# Risk Monitoring Messages (IDs 235-244)
235 - GetBBBO
236 - GetPrice
237 - PriceInfo
238 - GetOffTrPrice
239 - OffTrPriceInfo
240 - RiskQueryResponse
241-244 - Reserved for future risk messages
```

## See Also

- `cfsm/include/cfsm/Message.hpp` - Base message class definition
- `cfsm/include/cfsm/CFSM.hpp` - Message dispatching logic
- Component-specific `msg/` directories for existing messages
